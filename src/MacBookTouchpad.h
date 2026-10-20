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
		fTouchPackets = fTouchStartX = fTouchStartY = 0;
		fTouchTravel = fTouchAlien = 0;
		fTapCandidate = fTapPress = false;
		Reset();
	}

	// A tap is one contact that comes and goes quickly without travelling.
	// The pad reports every 8ms (125Hz, measured on the hardware), so the
	// packet count is a stable clock: observed taps lasted 7-11 packets
	// (48-80ms) and a finger deliberately rested on the pad lasted 66 and 168
	// packets. 25 packets (200ms) sits well clear of both.
	//
	// kTapTravel is the distance from where the finger landed, not the sum of
	// every step, so sensor noise while it sits still cannot add up past the
	// limit. The pad resolves roughly 90 units per millimetre, making 256 about
	// 2.8mm: the roll of a finger tapping, not a gesture. At 16 raw units per
	// pointer count that is also 16 pointer counts, so a tap that nudges the
	// cursor a pixel or two is nowhere near the limit.
	//
	// kTapAlien forgives a packet or two where the pad splits one fingertip
	// into two blobs, which otherwise ends the tap outright. A real two-finger
	// gesture reports two contacts for its whole length, far past this.
	//
	// Measured on the hardware over 34 touches: every tap travelled 10-74
	// units (0 to 4 pointer counts) and lasted 2-14 packets, while every
	// touch that was not a tap travelled 667-4755 units (41 to 297 counts).
	// Nothing landed in between, so 256 sits in an empty gap with room on
	// both sides. The same run never once saw a split fingertip, so kTapAlien
	// is headroom rather than something the limits depend on.
	enum { kTapPackets = 37, kTapTravel = 256, kTapAlien = 2 };

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
		// Tap to click. The pad sends no button edge for a tap, so both edges
		// are synthesised here. It reports exactly two contact-free packets
		// after every lift (measured, without exception), which is what makes
		// this safe: the press rides the first and the release the second, so
		// the button can never be left down waiting for a packet that the
		// hardware has no reason to send.
		if (fTapPress) {
			// Second contact-free packet: report[0] already holds the physical
			// button, which is the release.
			fTapPress = false;
		} else if (contacts == 0 && fTapCandidate) {
			report[0] = 1;
			fTapPress = true;
		}
		// Only a lone finger with the button up can still become a tap. The
		// physical button ends it outright, since the pad already reported that
		// click. Stray extra contacts are forgiven up to kTapAlien packets.
		// -1 keeps a touch ended until the pad goes clear again.
		if (contacts == 0) {
			fTouchPackets = fTouchAlien = fTouchTravel = 0;
		} else if (report[0] != 0) {
			fTouchPackets = -1;
		} else if (fTouchPackets >= 0) {
			if (contacts != 1 && ++fTouchAlien > kTapAlien)
				fTouchPackets = -1;
			else
				fTouchPackets++;
		}
		if (fTouchPackets == 1) {
			// The finger just landed: remember where, to measure travel from.
			fTouchStartX = x;
			fTouchStartY = y;
		}
		if (fTouchPackets > 0)
			fTouchTravel = Travel(x - fTouchStartX, y - fTouchStartY);
		fTapCandidate = fTouchPackets > 0 && fTouchPackets <= kTapPackets
			&& fTouchTravel <= kTapTravel;
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
		// Movement is never held back waiting to see whether the touch becomes
		// a tap. Holding it and releasing it in one packet was tried and makes
		// the pointer leap: a whole gesture's worth of travel arrives as a
		// single 8ms delta, and pointer acceleration multiplies large deltas.
		// Taps stay still because they barely move, not because we hide it.
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
	static int
	Travel(int dx, int dy)
	{
		return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
	}

	// Tap state deliberately survives Reset(): a lift resets the motion
	// tracking, and the tap is decided exactly then.
	int fTouchPackets;
	int fTouchStartX, fTouchStartY;
	int fTouchTravel;
	int fTouchAlien;
	bool fTapCandidate;
	bool fTapPress;
};

#endif
