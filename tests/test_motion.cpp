// SPDX-License-Identifier: MIT
#include "../src/MacBookTouchpad.h"
#include <assert.h>
#include <algorithm>
#include <stdio.h>
#include <vector>

static void
put(uint8_t* p, int x)
{
	p[0] = x & 255;
	p[1] = (uint16_t)x >> 8;
}
static int
get(const uint8_t* p)
{
	return (int16_t)(p[0] | p[1] << 8);
}
static std::vector<uint8_t>
packet(int x, int y, int count = 1, int origin = 1)
{
	std::vector<uint8_t> p(30 + count * 28);
	for (int i = 0; i < count; i++) {
		uint8_t* f = &p[30 + 28 * i];
		put(f, origin);
		put(f + 2, x);
		put(f + 4, y);
		put(f + 16, 100);
	}
	return p;
}
static void
feed(MacBookTouchpad& t, std::vector<uint8_t> p, uint8_t* out)
{
	assert(t.Convert(p.data(), p.size(), out));
}
int
main()
{
	uint8_t out[MacBookTouchpad::kReportSize] = {};
	// All quadrants; tiny diagonal movement must accumulate, even through
	// stationary packets. Check conservation, not implementation details.
	for (int sx : {-1, 1})
		for (int sy : {-1, 1}) {
			MacBookTouchpad t;
			int x = 0, y = 0;
			feed(t, packet(-100, 100), out);
			for (int i = 1; i <= 160; i++) {
				feed(t, packet(-100 + i * sx, 100 + i * sy), out);
				x += get(out + 1);
				y += get(out + 3);
				feed(t, packet(-100 + i * sx, 100 + i * sy), out);
				assert(get(out + 1) == 0 && get(out + 3) == 0);
			}
			assert(x == 10 * sx && y == -10 * sy);
		}
	// Shallow-angle travel: small axis must never be discarded or snapped.
	MacBookTouchpad t;
	feed(t, packet(0, 0), out);
	int x = 0, y = 0;
	for (int i = 1; i <= 160; i++) {
		feed(t, packet(i * 4, i), out);
		x += get(out + 1);
		y += get(out + 3);
	}
	assert(x == 40 && y == -10);
	// Lift/re-touch and extra contact do not teleport the pointer.
	feed(t, packet(0, 0, 0), out);
	feed(t, packet(3000, 3000), out);
	assert(get(out + 1) == 0 && get(out + 3) == 0);
	feed(t, packet(100, 100, 2), out);
	feed(t, packet(-3000, -3000), out);
	assert(get(out + 1) == 0 && get(out + 3) == 0);
	// Physical button works even with no finger coordinates.
	auto p = packet(0, 0, 0);
	p[15] = 1;
	feed(t, p, out);
	assert(out[0] == 1);
	p[15] = 0;
	feed(t, p, out);
	assert(out[0] == 0);
	// Malformed/truncated/oversized records never read out of bounds.
	for (size_t n = 0; n < 600; n++) {
		std::vector<uint8_t> bad(n);
		bool valid = t.Convert(bad.data(), n, out);
		assert(valid == (n >= 30 && n <= 478 && (n - 30) % 28 == 0));
	}
	// Contact change marker and large coordinate jumps re-anchor.
	feed(t, packet(100, 100, 1, 0), out);
	feed(t, packet(200, 200), out);
	assert(get(out + 1) == 0);
	feed(t, packet(3000, 3000), out);
	assert(get(out + 1) == 0);
	// Two-finger movement: both axes, signs, fractions and stationary frames.
	for (int sx : {-1, 1})
		for (int sy : {-1, 1}) {
			MacBookTouchpad scroll;
			feed(scroll, packet(0, 0, 2), out);
			int wheel = 0, pan = 0;
			for (int i = 1; i <= 512; i++) {
				feed(scroll, packet(i * sx, i * sy, 2), out);
				assert(get(out + 1) == 0 && get(out + 3) == 0);
				wheel += get(out + 5);
				pan += get(out + 7);
				feed(scroll, packet(i * sx, i * sy, 2), out);
				assert(get(out + 5) == 0 && get(out + 7) == 0);
			}
			assert(wheel == 4 * sy && pan == 4 * sx);
		}
	// Distinct contacts: ordering cannot change the centroid. One stationary
	// finger halves the speed; spreading apart must not scroll.
	MacBookTouchpad scroll;
	p = packet(-500, 100, 2);
	put(p.data() + 60, 500);
	feed(scroll, p, out);
	std::swap_ranges(p.begin() + 30, p.begin() + 58, p.begin() + 58);
	feed(scroll, p, out);
	assert(get(out + 5) == 0 && get(out + 7) == 0);
	put(p.data() + 32, 756);
	feed(scroll, p, out);
	assert(get(out + 7) == 1);
	put(p.data() + 32, 884);
	put(p.data() + 60, -628);
	feed(scroll, p, out);
	assert(get(out + 7) == 0);
	// Partial scroll must be discarded after lift, 3 fingers, malformed data,
	// a replacement marker on either contact, or switching to pointer motion.
	for (int reset = 0; reset < 6; reset++) {
		scroll.Reset();
		feed(scroll, packet(0, 0, 2), out);
		feed(scroll, packet(127, 127, 2), out);
		assert(get(out + 5) == 0 && get(out + 7) == 0);
		if (reset < 2)
			feed(scroll, packet(1000, 1000, reset), out);
		else if (reset == 2 || reset == 3)
			feed(scroll, packet(1000, 1000, 3), out);
		else if (reset == 4) {
			p = packet(1000, 1000, 2);
			put(p.data() + 30, 0);
			feed(scroll, p, out);
		} else
			assert(!scroll.Convert(p.data(), 29, out));
		feed(scroll, packet(0, 0, 2), out);
		assert(get(out + 5) == 0 && get(out + 7) == 0);
		feed(scroll, packet(1, 1, 2), out);
		assert(get(out + 5) == 0 && get(out + 7) == 0);
	}
	// Large jumps re-anchor; inactive records do not count as fingers.
	scroll.Reset();
	feed(scroll, packet(0, 0, 2), out);
	feed(scroll, packet(3000, 3000, 2), out);
	assert(get(out + 5) == 0 && get(out + 7) == 0);
	p = packet(3128, 3128, 3);
	put(p.data() + 30 + 2 * 28 + 16, 0);
	// Pressing the button switches two contacts from scrolling to dragging:
	// re-anchor, so neither the wheel nor the pointer jumps on that packet.
	p[15] = 1;
	feed(scroll, p, out);
	assert(out[0] == 1);
	assert(get(out + 1) == 0 && get(out + 3) == 0);
	assert(get(out + 5) == 0 && get(out + 7) == 0);
	// Releasing it switches back, again without a jump.
	p[15] = 0;
	feed(scroll, p, out);
	assert(out[0] == 0 && get(out + 5) == 0 && get(out + 7) == 0);
	assert(get(out + 1) == 0 && get(out + 3) == 0);
	// Dragging: a thumb holds the button while the index finger moves. Two
	// contacts must move the pointer, not scroll, and at the one-finger speed.
	for (int sx : {-1, 1})
		for (int sy : {-1, 1}) {
			MacBookTouchpad drag;
			int dragX = 0, dragY = 0;
			p = packet(0, 0, 2);
			put(p.data() + 58 + 2, 400);	// a thumb, parked elsewhere
			put(p.data() + 58 + 4, -400);
			p[15] = 1;
			feed(drag, p, out);
			for (int i = 1; i <= 160; i++) {
				put(p.data() + 32, i * sx);	// only the index finger moves
				put(p.data() + 34, i * sy);
				feed(drag, p, out);
				assert(out[0] == 1);
				assert(get(out + 5) == 0 && get(out + 7) == 0);
				dragX += get(out + 1);
				dragY += get(out + 3);
			}
			assert(dragX == 10 * sx && dragY == -10 * sy);
		}
	// The same travel by one finger alone gives the same pointer output, so a
	// drag does not run at half speed just because the thumb is resting.
	MacBookTouchpad single;
	int singleX = 0, singleY = 0;
	feed(single, packet(0, 0), out);
	for (int i = 1; i <= 160; i++) {
		feed(single, packet(i, i), out);
		singleX += get(out + 1);
		singleY += get(out + 3);
	}
	assert(singleX == 10 && singleY == -10);
	// A drag that starts before the button does not scroll once it is pressed.
	MacBookTouchpad late;
	feed(late, packet(0, 0, 2), out);
	p = packet(128, 128, 2);
	feed(late, p, out);
	assert(get(out + 5) != 0 || get(out + 7) != 0);
	p[15] = 1;
	feed(late, p, out);
	assert(get(out + 1) == 0 && get(out + 5) == 0);
	put(p.data() + 32, 128 + 16);
	feed(late, p, out);
	assert(out[0] == 1 && get(out + 1) == 1);
	assert(get(out + 5) == 0 && get(out + 7) == 0);
	// Tap to click. The pad sends two contact-free packets after every lift,
	// so the synthesised press rides the first and the release the second.
	auto lift = [&](MacBookTouchpad& t, uint8_t* o) {
		feed(t, packet(0, 0, 0), o);	// first contact-free packet
		int press = o[0];
		feed(t, packet(0, 0, 0), o);	// second: must release
		assert(o[0] == 0);
		return press;
	};
	{	// A short stationary touch clicks, and clicks once per tap.
		MacBookTouchpad tap;
		for (int round = 0; round < 3; round++) {
			for (int i = 0; i < 9; i++)
				feed(tap, packet(500, 500), out);
			assert(lift(tap, out) == 1);
			// No further packet may re-assert the button.
			feed(tap, packet(0, 0, 0), out);
			assert(out[0] == 0);
		}
	}
	{	// Resting a finger is not a tap, however still it is held.
		MacBookTouchpad rest;
		for (int i = 0; i < 200; i++)
			feed(rest, packet(500, 500), out);
		assert(lift(rest, out) == 0);
	}
	{	// A short touch that travels is a flick, not a tap.
		MacBookTouchpad flick;
		for (int i = 0; i < 9; i++)
			feed(flick, packet(500 + i * 40, 500), out);
		assert(lift(flick, out) == 0);
	}
	{	// Two fingers never tap: that is a scroll, not a click.
		MacBookTouchpad two;
		for (int i = 0; i < 9; i++)
			feed(two, packet(500, 500, 2), out);
		assert(lift(two, out) == 0);
	}
	{	// A physical click is reported by the pad; do not add a second one.
		MacBookTouchpad phys;
		for (int i = 0; i < 9; i++) {
			auto q = packet(500, 500);
			q[15] = 1;
			feed(phys, q, out);
			assert(out[0] == 1);
		}
		assert(lift(phys, out) == 0);
	}
	{	// A tap right at the limit still clicks; one packet more does not.
		MacBookTouchpad edge;
		for (int i = 0; i < MacBookTouchpad::kTapPackets; i++)
			feed(edge, packet(500, 500), out);
		assert(lift(edge, out) == 1);
		MacBookTouchpad over;
		for (int i = 0; i < MacBookTouchpad::kTapPackets + 1; i++)
			feed(over, packet(500, 500), out);
		assert(lift(over, out) == 0);
	}
	{	// A tap that drifts still clicks: a finger rolls as it taps. This is
		// what made taps unreliable before kTapTravel was measured from the
		// landing point rather than summed over every step.
		for (int drift = 0; drift <= MacBookTouchpad::kTapTravel / 2; drift += 32) {
			MacBookTouchpad roll;
			for (int i = 0; i < 9; i++)
				feed(roll, packet(500 + drift * i / 8, 500 + drift * i / 8), out);
			assert(lift(roll, out) == 1);
		}
		// Past the limit it is a gesture, not a tap.
		MacBookTouchpad far;
		for (int i = 0; i < 9; i++)
			feed(far, packet(500 + 40 * i, 500 + 40 * i), out);
		assert(lift(far, out) == 0);
	}
	{	// Jitter around the landing point never adds up to a disqualification,
		// however long it goes on, because travel is a distance and not a sum.
		MacBookTouchpad jitter;
		for (int i = 0; i < MacBookTouchpad::kTapPackets; i++)
			feed(jitter, packet(500 + (i % 2 ? 40 : -40), 500), out);
		assert(lift(jitter, out) == 1);
	}
	puts("PASS: quadrants, fractional conservation, shallow angles, lift, contacts, buttons, "
		 "malformed reports, two-finger scrolling, centroid, transitions, tap to click, "
		 "tap drift tolerance");
}
