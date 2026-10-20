#!/usr/bin/env python3
"""Compile actual host detection and wheel mapping with a mock SMBIOS module."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/MouseProtocolHandler.cpp').read_text()
gate = source.split('\tsmbios_module_info* smbios;', 1)[1].split('\n\tuint32 buttonCount', 1)[0]
gate = '\tsmbios_module_info* smbios;' + gate
mapping = source.split('\tinfo->wheel_ydelta =', 1)[1].split('\n\n\treturn B_OK;', 1)[0]
mapping = '\tinfo->wheel_ydelta =' + mapping
code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "MacBookTouchpad.h"
typedef uint32_t uint32;
struct module_info {};
struct smbios_module_info { bool (*match_vendor_product)(const char*, const char*); };
static const char* vendor;
static const char* product;
static bool available;
static int released;
static bool matches(const char* v, const char* p) {
    return vendor && product && !strcmp(v, vendor) && !strcmp(p, product);
}
static smbios_module_info mock = {matches};
static int get_module(const char*, module_info** result) {
    *result = reinterpret_cast<module_info*>(&mock);
    return available ? 0 : -1;
}
static void put_module(const char*) { released++; }
#define SMBIOS_MODULE_NAME "mock"
#define B_OK 0
static bool detect() {
    bool fReverseScroll = false;
GATE
    return fReverseScroll;
}
struct Movement { int32_t wheel_ydelta, wheel_xdelta; };
static Movement convert(bool fReverseScroll, uint32 y, uint32 x) {
    uint32 wheelData[2] = {y, x};
    Movement result;
    Movement* info = &result;
MAPPING
    return result;
}
int main() {
    const char* vendors[] = {nullptr, "Apple Inc.", "Other"};
    const char* products[] = {nullptr, "MacBook7,1", "MacBook7,10", "Other"};
    for (auto v : vendors) for (auto p : products) for (bool a : {false, true}) {
        vendor = v; product = p; available = a; released = 0;
        bool expected = a && v && p && !strcmp(v,"Apple Inc.") && !strcmp(p,"MacBook7,1");
        assert(detect() == expected);
        assert(released == (a ? 1 : 0));
    }
    for (int y : {-8,-1,0,1,8}) for (int x : {-8,-1,0,1,8}) {
        Movement normal = convert(false, y, x);
        Movement mac = convert(true, y, x);
        assert(normal.wheel_ydelta == -y && normal.wheel_xdelta == x);
        assert(mac.wheel_ydelta == y && mac.wheel_xdelta == -x);
    }
    // The synthetic touchpad report follows the same policy exactly once.
    MacBookTouchpad pad;
    uint8_t packet[86] = {}, report[MacBookTouchpad::kReportSize];
    for (int off : {30,58}) { packet[off] = 1; packet[off+16] = 100; }
    assert(pad.Convert(packet, sizeof(packet), report));
    for (int off : {30,58}) { packet[off+2] = 128; packet[off+4] = 128; }
    assert(pad.Convert(packet, sizeof(packet), report));
    int y = (int16_t)(report[5] | report[6] << 8);
    int x = (int16_t)(report[7] | report[8] << 8);
    Movement natural = convert(true, y, x);
    assert(natural.wheel_ydelta == 1 && natural.wheel_xdelta == -1);
}
'''
code = '#include <initializer_list>\n' + code.replace('GATE', gate).replace('MAPPING', mapping)
with tempfile.TemporaryDirectory() as d:
    cpp = Path(d) / 'policy.cpp'
    cpp.write_text(code)
    binary = str(Path(d) / 'policy')
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) +
                   shlex.split(os.environ.get('CXXFLAGS', '')) +
                   ['-std=c++11', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'src'),
                    str(cpp), '-o', binary], check=True)
    subprocess.run([binary], check=True)
print('PASS: exact host gate, missing SMBIOS, generic mouse axes and touchpad single inversion')
