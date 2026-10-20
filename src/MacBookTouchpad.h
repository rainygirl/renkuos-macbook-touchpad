// SPDX-License-Identifier: MIT
// Copyright 2026 rainygirl
#ifndef MACBOOK_TOUCHPAD_H
#define MACBOOK_TOUCHPAD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Wellspring type-2 wire format: 30-byte header, 28-byte contacts.
// Hardware protocol facts are documented in PROVENANCE.md. No Linux code copied.
class MacBookTouchpad
{
public:
	enum { kReportSize = 9 };

	MacBookTouchpad()
	{
		Reset();
	}

	void
	Reset()
	{
		fContacts = 0;
		fScroll = false;
		fX = fY = fRemainderX = fRemainderY = 0;
	}

	// Unnumbered report: button, signed LE16 X/Y, Wheel, AC Pan.
	// Caller supplies at least kReportSize bytes.
	bool
	Convert(const uint8_t* data, size_t size, uint8_t* report)
	{
		memset(report, 0, kReportSize);
		if (size < 30 || size > 30 + 16 * 28 || (size - 30) % 28 != 0) {
			Reset();
			return false;
		}
		report[0] = data[15] & 1;
		int contacts = 0;
		int x = 0, y = 0;
		bool changed = false;
		for (size_t offset = 30; offset < size; offset += 28) {
			if (Read16(data + offset + 16) <= 0)
				continue;
			contacts++;
			changed |= Read16(data + offset) == 0;
			x += Read16(data + offset + 2);
			y += Read16(data + offset + 4);
		}
		// Ignore unsupported gestures; keep the physical button report.
		if (contacts < 1 || contacts > 2 || changed) {
			Reset();
			return true;
		}
		// The integrated button turns a gesture into a drag, and dragging a
		// clickpad normally needs a second contact: a thumb holds the button
		// down while the index finger moves. So two contacts must keep moving
		// the pointer while the button is held. Scrolling is for two contacts
		// with the button released.
		const bool scroll = report[0] == 0 && contacts == 2;
		// Two contacts use the sum (twice the centroid), avoiding rounding
		// and dependence on contact record order. Reset fractions on transitions,
		// including the pointer/scroll switch when the button changes.
		if (fContacts != contacts || fScroll != scroll) {
			Reset();
			fContacts = contacts;
			fScroll = scroll;
			fX = x;
			fY = y;
			return true;
		}
		int dx = x - fX;
		int dy = fY - y; // HID mouse Y grows downwards.
		fX = x;
		fY = y;
		const int jumpLimit = 1024 * contacts;
		if (dx > jumpLimit || dx < -jumpLimit
			|| dy > jumpLimit || dy < -jumpLimit) {
			fRemainderX = fRemainderY = 0;
			return true;
		}
		// Preserve fractions on both axes, including packets with a zero
		// component. No axis lock and no per-axis movement threshold.
		int sumX = dx + fRemainderX;
		int sumY = dy + fRemainderY;
		// One wheel step per 128 raw centroid units. Fractions persist
		// across stationary reports, but never across gestures.
		// x and y are sums, so one moving contact displaces the sum by its own
		// travel whatever the contact count: a thumb-and-finger drag keeps the
		// same pointer speed as one finger.
		const int divisor = scroll ? 2 * 128 : 16;
		int outX = sumX / divisor;
		int outY = sumY / divisor;
		fRemainderX = sumX - outX * divisor;
		fRemainderY = sumY - outY * divisor;
		if (!scroll) {
			Write16(report + 1, outX);
			Write16(report + 3, outY);
		} else {
			// Emit standard wheel directions; the shared mouse handler applies
			// the MacBook host preference to all USB mice and this touchpad.
			// Haiku negates Wheel, but passes AC Pan through unchanged.
			Write16(report + 5, -outY);
			Write16(report + 7, outX);
		}
		return true;
	}

private:
	static int
	Read16(const uint8_t* p)
	{
		int value = p[0] | (p[1] << 8);
		return value >= 32768 ? value - 65536 : value;
	}

	static void
	Write16(uint8_t* p, int value)
	{
		uint16_t bits = (uint16_t)value;
		p[0] = bits & 255;
		p[1] = bits >> 8;
	}

	int fContacts;
	bool fScroll;
	int fX, fY;
	int fRemainderX, fRemainderY;
};

#endif
