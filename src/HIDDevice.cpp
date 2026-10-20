/*
 * Copyright 2008-2011, Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for USB Human Interface Devices.


#include "Driver.h"
#include "HIDDevice.h"
#include "HIDReport.h"
#include "HIDWriter.h"
#include "ProtocolHandler.h"
#include "QuirkyDevices.h"

#include <usb/USB_hid.h>
#include <smbios.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <new>

HIDDevice::HIDDevice(usb_device device, const usb_configuration_info *config,
	size_t interfaceIndex, int32 quirkyIndex)
	:	fStatus(B_NO_INIT),
		fDevice(device),
		fInterruptPipe(0),
		fInterfaceIndex(interfaceIndex),
		fTransferScheduled(0),
		fTransferBufferSize(0),
		fTransferBuffer(NULL),
		fParentCookie(-1),
		fOpenCount(0),
		fRemoved(false),
		fMacBook71Keyboard(false),
		fMacBook71Touchpad(false),
		fParser(this),
		fProtocolHandlerCount(0),
		fProtocolHandlerList(NULL)
{
	mutex_init(&fTouchpadLock, "MacBook touchpad mode");
	memset(fMouseReport, 0, sizeof(fMouseReport));
	uint8 *reportDescriptor = NULL;
	size_t descriptorLength = 0;

	const usb_device_descriptor *deviceDescriptor
		= gUSBModule->get_device_descriptor(device);

	// This product ID is shared with other Macs: the SMBIOS check is required.
	if (deviceDescriptor != NULL && deviceDescriptor->vendor_id == 0x05ac
		&& deviceDescriptor->product_id == 0x0236
		&& (interfaceIndex == 0 || interfaceIndex == 2)) {
		smbios_module_info* smbios;
		if (get_module(SMBIOS_MODULE_NAME, (module_info**)&smbios) == B_OK) {
			bool matches = smbios->match_vendor_product("Apple Inc.", "MacBook7,1");
			fMacBook71Keyboard = matches && interfaceIndex == 0;
			fMacBook71Touchpad = matches && interfaceIndex == 2;
			put_module(SMBIOS_MODULE_NAME);
		}
	}

	// The mouse interface owns the pointer. Read the raw endpoint on
	// interface 1 instead of firmware-emulated relative reports on interface 2.
	usb_pipe touchpadPipe = 0;
	if (fMacBook71Touchpad && config->interface_count > 2) {
		const usb_interface_info* raw = config->interface[1].active;
		if (raw != NULL) {
			for (size_t i = 0; i < raw->endpoint_count; i++) {
				const usb_endpoint_descriptor* endpoint = raw->endpoint[i].descr;
				if (endpoint->endpoint_address == 0x81
					&& (endpoint->attributes & USB_ENDPOINT_ATTR_MASK)
						== USB_ENDPOINT_ATTR_INTERRUPT) {
					touchpadPipe = raw->endpoint[i].handle;
					break;
				}
			}
		}
	}
	fMacBook71Touchpad = touchpadPipe != 0;

	HIDWriter descriptorWriter;
	bool hasFixedDescriptor = false;
	if (quirkyIndex >= 0) {
		quirky_build_descriptor quirkyBuildDescriptor
			= gQuirkyDevices[quirkyIndex].build_descriptor;

		if (quirkyBuildDescriptor != NULL
			&& quirkyBuildDescriptor(descriptorWriter) == B_OK) {

			reportDescriptor = (uint8 *)descriptorWriter.Buffer();
			descriptorLength = descriptorWriter.BufferLength();
			hasFixedDescriptor = true;
		}
	}

	if (fMacBook71Touchpad) {
		// Button, padding, signed LE16 relative X/Y, Wheel and AC Pan.
		static const uint8 descriptor[] = {
			0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
			0x05, 0x09, 0x09, 0x01, 0x15, 0x00, 0x25, 0x01,
			0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
			0x75, 0x07, 0x95, 0x01, 0x81, 0x01,
			0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
			0x16, 0x00, 0x80, 0x26, 0xff, 0x7f,
			0x75, 0x10, 0x95, 0x02, 0x81, 0x06,
			0x09, 0x38, 0x95, 0x01, 0x81, 0x06,
			0x05, 0x0c, 0x0a, 0x38, 0x02, 0x81, 0x06, 0xc0, 0xc0
		};
		descriptorWriter.Reset();
		if (descriptorWriter.Write(descriptor, sizeof(descriptor)) != B_OK) {
			fStatus = B_NO_MEMORY;
			return;
		}
		reportDescriptor = (uint8*)descriptorWriter.Buffer();
		descriptorLength = descriptorWriter.BufferLength();
		hasFixedDescriptor = true;
	}

	if (!hasFixedDescriptor) {
		// Conforming device, find the HID descriptor and get the report
		// descriptor from the device.
		usb_hid_descriptor *hidDescriptor = NULL;

		const usb_interface_info *interfaceInfo
			= config->interface[interfaceIndex].active;
		for (size_t i = 0; i < interfaceInfo->generic_count; i++) {
			const usb_generic_descriptor &generic
				= interfaceInfo->generic[i]->generic;
			if (generic.descriptor_type == B_USB_HID_DESCRIPTOR_HID) {
				hidDescriptor = (usb_hid_descriptor *)&generic;
				descriptorLength
					= hidDescriptor->descriptor_info[0].descriptor_length;
				break;
			}
		}

		if (hidDescriptor == NULL) {
			TRACE_ALWAYS("didn't find a HID descriptor in the configuration, "
				"trying to retrieve manually\n");

			descriptorLength = sizeof(usb_hid_descriptor);
			hidDescriptor = (usb_hid_descriptor *)malloc(descriptorLength);
			if (hidDescriptor == NULL) {
				TRACE_ALWAYS("failed to allocate buffer for hid descriptor\n");
				fStatus = B_NO_MEMORY;
				return;
			}

			status_t result = gUSBModule->send_request(device,
				USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
				USB_REQUEST_GET_DESCRIPTOR,
				B_USB_HID_DESCRIPTOR_HID << 8, interfaceIndex, descriptorLength,
				hidDescriptor, &descriptorLength);

			TRACE("get hid descriptor: result: 0x%08" B_PRIx32 "; length: %lu"
				"\n", result, descriptorLength);
			if (result == B_OK) {
				descriptorLength
					= hidDescriptor->descriptor_info[0].descriptor_length;
			} else {
				descriptorLength = 256; /* XXX */
				TRACE_ALWAYS("failed to get HID descriptor, trying with a "
					"fallback report descriptor length of %lu\n",
					descriptorLength);
			}

			free(hidDescriptor);
		}

		reportDescriptor = (uint8 *)malloc(descriptorLength);
		if (reportDescriptor == NULL) {
			TRACE_ALWAYS("failed to allocate buffer for report descriptor\n");
			fStatus = B_NO_MEMORY;
			return;
		}

		status_t result = gUSBModule->send_request(device,
			USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
			USB_REQUEST_GET_DESCRIPTOR,
			B_USB_HID_DESCRIPTOR_REPORT << 8, interfaceIndex, descriptorLength,
			reportDescriptor, &descriptorLength);

		TRACE("get report descriptor: result: 0x%08" B_PRIx32 "; length: %"
			B_PRIuSIZE "\n", result, descriptorLength);
		if (result != B_OK) {
			TRACE_ALWAYS("failed tot get report descriptor\n");
			free(reportDescriptor);
			return;
		}
	} else {
		TRACE_ALWAYS("found quirky device, using patched descriptor\n");
	}

#if 1
	// save report descriptor for troubleshooting
	char outputFile[128];
	sprintf(outputFile, "/tmp/usb_hid_report_descriptor_%04x_%04x_%lu.bin",
		deviceDescriptor->vendor_id, deviceDescriptor->product_id,
		interfaceIndex);
	int fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		write(fd, reportDescriptor, descriptorLength);
		close(fd);
	}
#endif

	status_t result = fParser.ParseReportDescriptor(reportDescriptor,
		descriptorLength);
	if (!hasFixedDescriptor)
		free(reportDescriptor);

	if (result != B_OK) {
		TRACE_ALWAYS("parsing the report descriptor failed\n");
		fStatus = result;
		return;
	}

#if 0
	for (uint32 i = 0; i < fParser.CountReports(HID_REPORT_TYPE_ANY); i++)
		fParser.ReportAt(HID_REPORT_TYPE_ANY, i)->PrintToStream();
#endif

	// find the interrupt in pipe
	usb_interface_info *interface = config->interface[interfaceIndex].active;
	for (size_t i = 0; i < interface->endpoint_count; i++) {
		usb_endpoint_descriptor *descriptor = interface->endpoint[i].descr;
		if ((descriptor->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
			&& (descriptor->attributes & USB_ENDPOINT_ATTR_MASK)
				== USB_ENDPOINT_ATTR_INTERRUPT) {
			fEndpointAddress = descriptor->endpoint_address;
			fInterruptPipe = interface->endpoint[i].handle;
			break;
		}
	}

	if (fMacBook71Touchpad) {
		fInterruptPipe = touchpadPipe;
		fEndpointAddress = 0x81;
	}

	if (fInterruptPipe == 0) {
		TRACE_ALWAYS("didn't find a suitable interrupt pipe\n");
		return;
	}

	fTransferBufferSize = fMacBook71Touchpad ? 512
		: fParser.MaxReportSize(HID_REPORT_TYPE_INPUT);
	if (fTransferBufferSize == 0) {
		TRACE_ALWAYS("report claims a report size of 0\n");
		return;
	}

	// We pad the allocation size so that we can always read 32 bits at a time
	// (as done in HIDReportItem) without the need for an additional boundary
	// check. We don't increase the transfer buffer size though as to not expose
	// this implementation detail onto the device when scheduling transfers.
	fTransferBuffer = (uint8 *)malloc(fTransferBufferSize + 3);
	if (fTransferBuffer == NULL) {
		TRACE_ALWAYS("failed to allocate transfer buffer\n");
		fStatus = B_NO_MEMORY;
		return;
	}

	if (quirkyIndex >= 0) {
		quirky_init_function quirkyInit
			= gQuirkyDevices[quirkyIndex].init_function;

		if (quirkyInit != NULL) {
			fStatus = quirkyInit(device, config, interfaceIndex);
			if (fStatus != B_OK)
				return;
		}
	}

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList,
		fProtocolHandlerCount);
	fStatus = B_OK;
}


HIDDevice::~HIDDevice()
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		ProtocolHandler *next = handler->NextHandler();
		delete handler;
		handler = next;
	}

	free(fTransferBuffer);
	mutex_destroy(&fTouchpadLock);
}


void
HIDDevice::SetParentCookie(int32 cookie)
{
	fParentCookie = cookie;
}


status_t
HIDDevice::Open(ProtocolHandler *handler, uint32 flags)
{
	mutex_lock(&fTouchpadLock);
	if (fMacBook71Touchpad && fOpenCount == 0) {
		status_t status = _SetTouchpadMode(true);
		if (status != B_OK) {
			mutex_unlock(&fTouchpadLock);
			return status;
		}
		fTouchpad.Reset();
	}
	atomic_add(&fOpenCount, 1);
	mutex_unlock(&fTouchpadLock);
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler *handler)
{
	mutex_lock(&fTouchpadLock);
	atomic_add(&fOpenCount, -1);
	gUSBModule->cancel_queued_transfers(fInterruptPipe);
		// This will wake up any listeners. Whether they should close or retry
		// is handeled internally by the handlers.
	if (fMacBook71Touchpad && fOpenCount == 0 && !fRemoved)
		_SetTouchpadMode(false);
	mutex_unlock(&fTouchpadLock);
	return B_OK;
}


void
HIDDevice::Removed()
{
	fRemoved = true;
	gUSBModule->cancel_queued_transfers(fInterruptPipe);
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport*)
{
	if (fRemoved)
		return ENODEV;

	if (atomic_get_and_set(&fTransferScheduled, 1) != 0) {
		// someone else already caused a transfer to be scheduled
		return B_OK;
	}

	TRACE("scheduling interrupt transfer of %lu bytes\n", fTransferBufferSize);
	status_t result = gUSBModule->queue_interrupt(fInterruptPipe,
		fTransferBuffer, fTransferBufferSize, _TransferCallback, this);
	if (result != B_OK) {
		TRACE_ALWAYS("failed to schedule interrupt transfer 0x%08" B_PRIx32
			"\n", result);
		atomic_set(&fTransferScheduled, 0);
		return result;
	}

	return B_OK;
}


status_t
HIDDevice::SendReport(HIDReport *report)
{
	size_t actualLength;
	return gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_REPORT, 0x200 | report->ID(), fInterfaceIndex,
		report->ReportSize(), report->CurrentReport(), &actualLength);
}


ProtocolHandler *
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}


void
HIDDevice::_UnstallCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	HIDDevice *device = (HIDDevice *)cookie;
	if (status != B_OK) {
		TRACE_ALWAYS("Unable to unstall device: %s\n", strerror(status));
	}

	// Now report the original failure, since we're ready to retry
	_TransferCallback(cookie, B_ERROR, device->fTransferBuffer, 0);
}


void
HIDDevice::_TransferCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	HIDDevice *device = (HIDDevice *)cookie;
	if (status == B_DEV_STALLED && !device->fRemoved) {
		// try clearing stalls right away, the report listeners will resubmit
		gUSBModule->queue_request(device->fDevice,
			USB_REQTYPE_STANDARD | USB_REQTYPE_ENDPOINT_OUT,
			USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT,
			device->fEndpointAddress, 0, NULL, _UnstallCallback, device);
		return;
	}

	uint8* report = device->fTransferBuffer;
	if (device->fMacBook71Touchpad && status == B_OK) {
		if (!device->fTouchpad.Convert(report, actualLength, device->fMouseReport))
			status = B_BAD_DATA;
		report = device->fMouseReport;
		actualLength = MacBookTouchpad::kReportSize;
	}
	atomic_set(&device->fTransferScheduled, 0);
	device->fParser.SetReport(status, report, actualLength);
}


status_t
HIDDevice::_SetTouchpadMode(bool enabled)
{
	uint8 mode[8];
	size_t actual = 0;
	status_t status = gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_GET_REPORT, 0x300, 0, sizeof(mode), mode, &actual);
	if (status != B_OK || actual != sizeof(mode))
		return status != B_OK ? status : B_BAD_DATA;
	mode[0] = enabled ? 0x01 : 0x08;
	status = gUSBModule->send_request(fDevice,
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_REPORT, 0x300, 0, sizeof(mode), mode, &actual);
	if (status != B_OK || actual != sizeof(mode))
		return status != B_OK ? status : B_BAD_DATA;
	TRACE_ALWAYS("MacBook7,1 touchpad: %s mode\n", enabled ? "raw" : "mouse");
	return B_OK;
}
