#!/usr/bin/env python3
# Copyright © 2025 Intel Corporation
# SPDX-License-Identifier: MIT

import argparse
import ctypes
import enum
import fcntl
import json
import pathlib
import os
import re
import sys


mesa_hwconfigs_of_interest = list('''
    INTEL_HWCONFIG_GEOMETRY_PIPES_PER_SLICE
    INTEL_HWCONFIG_MAX_DS_URB_ENTRIES
    INTEL_HWCONFIG_MAX_DUAL_SUBSLICES_SUPPORTED
    INTEL_HWCONFIG_MAX_GS_URB_ENTRIES
    INTEL_HWCONFIG_MAX_HS_URB_ENTRIES
    INTEL_HWCONFIG_MAX_NUM_EU_PER_DSS
    INTEL_HWCONFIG_MAX_SLICES_SUPPORTED
    INTEL_HWCONFIG_MAX_SUBSLICE
    INTEL_HWCONFIG_MAX_VS_URB_ENTRIES
    INTEL_HWCONFIG_NUM_PIXEL_PIPES
    INTEL_HWCONFIG_NUM_THREADS_PER_EU
    INTEL_HWCONFIG_TOTAL_DS_THREADS
    INTEL_HWCONFIG_TOTAL_GS_THREADS
    INTEL_HWCONFIG_TOTAL_HS_THREADS
    INTEL_HWCONFIG_TOTAL_PS_THREADS
    INTEL_HWCONFIG_TOTAL_VS_THREADS
    INTEL_HWCONFIG_URB_SIZE_PER_SLICE_IN_KB
'''.strip().replace("INTEL_HWCONFIG_", "").split())


class HwconfigTypes(enum.Enum):

    INTEL_HWCONFIG_MAX_SLICES_SUPPORTED = 1
    INTEL_HWCONFIG_MAX_DUAL_SUBSLICES_SUPPORTED = 2
    INTEL_HWCONFIG_MAX_NUM_EU_PER_DSS = 3
    INTEL_HWCONFIG_NUM_PIXEL_PIPES = 4
    INTEL_HWCONFIG_DEPRECATED_MAX_NUM_GEOMETRY_PIPES = 5
    INTEL_HWCONFIG_DEPRECATED_L3_CACHE_SIZE_IN_KB = 6
    INTEL_HWCONFIG_DEPRECATED_L3_BANK_COUNT = 7
    INTEL_HWCONFIG_L3_CACHE_WAYS_SIZE_IN_BYTES = 8
    INTEL_HWCONFIG_L3_CACHE_WAYS_PER_SECTOR = 9
    INTEL_HWCONFIG_MAX_MEMORY_CHANNELS = 10
    INTEL_HWCONFIG_MEMORY_TYPE = 11
    INTEL_HWCONFIG_CACHE_TYPES = 12
    INTEL_HWCONFIG_LOCAL_MEMORY_PAGE_SIZES_SUPPORTED = 13
    INTEL_HWCONFIG_DEPRECATED_SLM_SIZE_IN_KB = 14
    INTEL_HWCONFIG_NUM_THREADS_PER_EU = 15
    INTEL_HWCONFIG_TOTAL_VS_THREADS = 16
    INTEL_HWCONFIG_TOTAL_GS_THREADS = 17
    INTEL_HWCONFIG_TOTAL_HS_THREADS = 18
    INTEL_HWCONFIG_TOTAL_DS_THREADS = 19
    INTEL_HWCONFIG_TOTAL_VS_THREADS_POCS = 20
    INTEL_HWCONFIG_TOTAL_PS_THREADS = 21
    INTEL_HWCONFIG_DEPRECATED_MAX_FILL_RATE = 22
    INTEL_HWCONFIG_MAX_RCS = 23
    INTEL_HWCONFIG_MAX_CCS = 24
    INTEL_HWCONFIG_MAX_VCS = 25
    INTEL_HWCONFIG_MAX_VECS = 26
    INTEL_HWCONFIG_MAX_COPY_CS = 27
    INTEL_HWCONFIG_DEPRECATED_URB_SIZE_IN_KB = 28
    INTEL_HWCONFIG_MIN_VS_URB_ENTRIES = 29
    INTEL_HWCONFIG_MAX_VS_URB_ENTRIES = 30
    INTEL_HWCONFIG_MIN_PCS_URB_ENTRIES = 31
    INTEL_HWCONFIG_MAX_PCS_URB_ENTRIES = 32
    INTEL_HWCONFIG_MIN_HS_URB_ENTRIES = 33
    INTEL_HWCONFIG_MAX_HS_URB_ENTRIES = 34
    INTEL_HWCONFIG_MIN_GS_URB_ENTRIES = 35
    INTEL_HWCONFIG_MAX_GS_URB_ENTRIES = 36
    INTEL_HWCONFIG_MIN_DS_URB_ENTRIES = 37
    INTEL_HWCONFIG_MAX_DS_URB_ENTRIES = 38
    INTEL_HWCONFIG_PUSH_CONSTANT_URB_RESERVED_SIZE = 39
    INTEL_HWCONFIG_POCS_PUSH_CONSTANT_URB_RESERVED_SIZE = 40
    INTEL_HWCONFIG_URB_REGION_ALIGNMENT_SIZE_IN_BYTES = 41
    INTEL_HWCONFIG_URB_ALLOCATION_SIZE_UNITS_IN_BYTES = 42
    INTEL_HWCONFIG_MAX_URB_SIZE_CCS_IN_BYTES = 43
    INTEL_HWCONFIG_VS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT = 44
    INTEL_HWCONFIG_DS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT = 45
    INTEL_HWCONFIG_NUM_RT_STACKS_PER_DSS = 46
    INTEL_HWCONFIG_MAX_URB_STARTING_ADDRESS = 47
    INTEL_HWCONFIG_MIN_CS_URB_ENTRIES = 48
    INTEL_HWCONFIG_MAX_CS_URB_ENTRIES = 49
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_URB = 50
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_REST = 51
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_DC = 52
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RO = 53
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_Z = 54
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COLOR = 55
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_UNIFIED_TILE_CACHE = 56
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COMMAND_BUFFER = 57
    INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RW = 58
    INTEL_HWCONFIG_MAX_NUM_L3_CONFIGS = 59
    INTEL_HWCONFIG_BINDLESS_SURFACE_OFFSET_BIT_COUNT = 60
    INTEL_HWCONFIG_RESERVED_CCS_WAYS = 61
    INTEL_HWCONFIG_CSR_SIZE_IN_MB = 62
    INTEL_HWCONFIG_GEOMETRY_PIPES_PER_SLICE = 63
    INTEL_HWCONFIG_L3_BANK_SIZE_IN_KB = 64
    INTEL_HWCONFIG_SLM_SIZE_PER_DSS = 65
    INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_SLICE = 66
    INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_DSS = 67
    INTEL_HWCONFIG_URB_SIZE_PER_SLICE_IN_KB = 68
    INTEL_HWCONFIG_URB_SIZE_PER_L3_BANK_COUNT_IN_KB = 69
    INTEL_HWCONFIG_MAX_SUBSLICE = 70
    INTEL_HWCONFIG_MAX_EU_PER_SUBSLICE = 71
    INTEL_HWCONFIG_RAMBO_L3_BANK_SIZE_IN_KB = 72
    INTEL_HWCONFIG_SLM_SIZE_PER_SS_IN_KB = 73
    INTEL_HWCONFIG_NUM_HBM_STACKS_PER_TILE = 74
    INTEL_HWCONFIG_NUM_CHANNELS_PER_HBM_STACK = 75
    INTEL_HWCONFIG_HBM_CHANNEL_WIDTH_IN_BYTES = 76
    INTEL_HWCONFIG_MIN_TASK_URB_ENTRIES = 77
    INTEL_HWCONFIG_MAX_TASK_URB_ENTRIES = 78
    INTEL_HWCONFIG_MIN_MESH_URB_ENTRIES = 79
    INTEL_HWCONFIG_MAX_MESH_URB_ENTRIES = 80
    INTEL_HWCONFIG_MAX_GSC = 81
    INTEL_HWCONFIG_SYNC_NUM_RT_STACKS_PER_DSS = 82
    INTEL_HWCONFIG_NUM_XECU = 83

    @staticmethod
    def to_name(e):
        assert isinstance(e, (int, HwconfigTypes))
        if isinstance(e, HwconfigTypes):
            return e.name
        try:
            return HwconfigTypes(e).name
        except Exception:
            return e

    @staticmethod
    def to_value(e):
        if isinstance(e, int):
            return e
        elif isinstance(e, HwconfigTypes):
            return e.value
        else:
            try:
                return HwconfigTypes[e].value
            except Exception:
                return int(e)


class HwconfigMemTypes(enum.Enum):

    INTEL_HWCONFIG_MEMORY_TYPE_LPDDR4 = 0
    INTEL_HWCONFIG_MEMORY_TYPE_LPDDR5 = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_HBM2 = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_HBM2e = enum.auto()
    INTEL_HWCONFIG_MEMORY_TYPE_GDDR6 = enum.auto()


class HwconfigCacheTypes(enum.Enum):

    INTEL_HWCONFIG_CACHE_TYPE_L3 = 0
    INTEL_HWCONFIG_CACHE_TYPE_LLC = enum.auto()
    INTEL_HWCONFIG_CACHE_TYPE_EDRAM = enum.auto()


def ioc(d, t, n, s): return (d << 30) | (t << 8) | (n << 0) | (s << 16)


def io(t, n, s): return ioc(0, t, n, s)
def ior(t, n, s): return ioc(2, t, n, s)
def iow(t, n, s): return ioc(1, t, n, s)
def iowr(t, n, s): return ioc(3, t, n, s)


def drm_io(n, s): return io(ord('d'), n, s)
def drm_ior(n, s): return ior(ord('d'), n, s)
def drm_iow(n, s): return iow(ord('d'), n, s)
def drm_iowr(n, s): return iowr(ord('d'), n, s)


def drv_io(n, s): return drm_io(0x40 + n, s)
def drv_ior(n, s): return drm_ior(0x40 + n, s)
def drv_iow(n, s): return drm_iow(0x40 + n, s)
def drv_iowr(n, s): return drm_iowr(0x40 + n, s)


symver_re = re.compile(r"^(\d+(\.\d+)*)")
kernel_version = symver_re.match(os.uname().release).group(0)
kernel_version_tuple = tuple(int(v) for v in kernel_version.split("."))


class PyDrm:

    class drm_version(ctypes.Structure):
        _fields_ = [
            ("version_major", ctypes.c_int),  # Major version
            ("version_minor", ctypes.c_int),  # Minor version
            ("version_patchlevel", ctypes.c_int),  # Patch level
            ("name_len", ctypes.c_size_t),
            ("name",  ctypes.POINTER(ctypes.c_char)),  # Name of driver
            ("date_len", ctypes.c_size_t),
            ("date",  ctypes.POINTER(ctypes.c_char)),
            ("desc_len", ctypes.c_size_t),
            ("desc",  ctypes.POINTER(ctypes.c_char)),  # Driver description
        ]

    DRM_IOCTL_VERSION = drm_iowr(0x00, ctypes.sizeof(drm_version))

    @staticmethod
    def get_drm_version(fd):
        drm_ver = PyDrm.drm_version()
        # print([ (str(f), getattr(drm_ver, f)) for f, v in drm_ver._fields_ ])

        retval = fcntl.ioctl(fd, PyDrm.DRM_IOCTL_VERSION, drm_ver)
        if retval != 0:
            return None

        drm_ver.name = ctypes.create_string_buffer(drm_ver.name_len)
        drm_ver.date = ctypes.create_string_buffer(drm_ver.date_len)
        drm_ver.desc = ctypes.create_string_buffer(drm_ver.desc_len)
        assert fcntl.ioctl(fd, PyDrm.DRM_IOCTL_VERSION, drm_ver) == 0

        return {
            "version": (drm_ver.version_major, drm_ver.version_minor,
                        drm_ver.version_patchlevel),
            "name": ctypes.string_at(drm_ver.name, drm_ver.name_len).decode(),
            "date": ctypes.string_at(drm_ver.date, drm_ver.date_len).decode(),
            "desc": ctypes.string_at(drm_ver.desc, drm_ver.desc_len).decode(),
        }


class PyI915Drm(PyDrm):

    class drm_i915_query(ctypes.Structure):
        _fields_ = [
            # The number of elements in the @items_ptr array
            ("num_items", ctypes.c_uint32),

            # Unused for now. Must be cleared to zero.
            ("flags", ctypes.c_uint32),

            # Pointer to an array of struct drm_i915_query_item.
            ("items_ptr", ctypes.c_uint64),
        ]

    class drm_i915_query_item(ctypes.Structure):
        _fields_ = [
            # The id for this query.
            ("query_id", ctypes.c_uint64),

            # Size of the queried data
            ("length", ctypes.c_int32),

            ("flags", ctypes.c_uint32),

            # Data will be written at the location
            ("data_ptr", ctypes.c_uint64),
        ]

    DRM_I915_QUERY_TOPOLOGY_INFO = 1
    DRM_I915_QUERY_ENGINE_INFO = 2
    DRM_I915_QUERY_PERF_CONFIG = 3
    DRM_I915_QUERY_MEMORY_REGIONS = 4
    DRM_I915_QUERY_HWCONFIG_BLOB = 5
    DRM_I915_QUERY_GEOMETRY_SUBSLICES = 6
    DRM_I915_QUERY_GUC_SUBMISSION_VERSION = 7

    DRM_I915_QUERY_PERF_CONFIG_LIST = 1
    DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_UUID = 2
    DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_ID = 3

    DRM_IOCTL_I915_QUERY = drv_iowr(0x39, ctypes.sizeof(drm_i915_query))

    @staticmethod
    def get_hwconfig(fd):
        query_item = PyI915Drm.drm_i915_query_item()
        query_item.query_id = PyI915Drm.DRM_I915_QUERY_HWCONFIG_BLOB

        dev_query = PyI915Drm.drm_i915_query()
        dev_query.num_items = 1
        dev_query.items_ptr = ctypes.addressof(query_item)

        if fcntl.ioctl(fd, PyI915Drm.DRM_IOCTL_I915_QUERY, dev_query) != 0 or \
           query_item.length < 0:
            return None

        assert (query_item.length %
                4) == 0, f"query_item.length: {query_item.length}"

        hwconfig_blob = ctypes.create_string_buffer(query_item.length)
        query_item.data_ptr = ctypes.addressof(hwconfig_blob)

        assert fcntl.ioctl(fd, PyI915Drm.DRM_IOCTL_I915_QUERY,
                           dev_query) == 0

        return bytes(hwconfig_blob)


class PyXeDrm(PyDrm):

    class drm_xe_device_query(ctypes.Structure):
        _fields_ = [
            # Pointer to the first extension struct, if any
            ("extensions", ctypes.c_uint64),

            # The type of data to query
            ("query", ctypes.c_uint32),

            # Size of the queried data
            ("size", ctypes.c_uint32),

            # Queried data is placed here
            ("data", ctypes.c_uint64),

            # Reserved
            ("reserved", ctypes.c_uint64 * 2),
        ]

    DRM_XE_DEVICE_QUERY_ENGINES = 0
    DRM_XE_DEVICE_QUERY_MEM_REGIONS = 1
    DRM_XE_DEVICE_QUERY_CONFIG = 2
    DRM_XE_DEVICE_QUERY_GT_LIST = 3
    DRM_XE_DEVICE_QUERY_HWCONFIG = 4
    DRM_XE_DEVICE_QUERY_GT_TOPOLOGY = 5
    DRM_XE_DEVICE_QUERY_ENGINE_CYCLES = 6
    DRM_XE_DEVICE_QUERY_UC_FW_VERSION = 7
    DRM_XE_DEVICE_QUERY_OA_UNITS = 8

    DRM_IOCTL_XE_DEVICE_QUERY = drv_iowr(0, ctypes.sizeof(drm_xe_device_query))

    @staticmethod
    def get_hwconfig(fd):
        dev_query = PyXeDrm.drm_xe_device_query()
        dev_query.query = PyXeDrm.DRM_XE_DEVICE_QUERY_HWCONFIG

        if fcntl.ioctl(fd, PyXeDrm.DRM_IOCTL_XE_DEVICE_QUERY, dev_query) != 0:
            return None

        assert (dev_query.size % 4) == 0

        hwconfig_blob = ctypes.create_string_buffer(dev_query.size)
        dev_query.data = ctypes.addressof(hwconfig_blob)

        assert fcntl.ioctl(fd, PyXeDrm.DRM_IOCTL_XE_DEVICE_QUERY,
                           dev_query) == 0

        return bytes(hwconfig_blob)


class HwconfigItem:

    def __init__(self, key, values):
        assert isinstance(key, (int, HwconfigTypes))
        try:
            self.key = HwconfigTypes(key)
        except Exception:
            self.key = key
        self.values = values

    def to_dwords(self):
        result = []
        k = HwconfigTypes.to_value(self.key)
        length = len(self.values)
        result.append(k)
        result.append(length)
        result += self.values
        return result

    @property
    def name(self):
        return HwconfigTypes.to_name(self.key)

    @property
    def value(self):
        assert len(self.values) == 1
        return self.values[0]

    def for_humans(self):
        k = HwconfigTypes.to_name(self.key)
        return [k, len(self.values)] + self.values


class Hwconfig:

    def __init__(self, hwconfig_bytes):
        assert len(hwconfig_bytes) % 4 == 0
        self._bytes = hwconfig_bytes

    @staticmethod
    def blob_to_ints(blob):
        if issubclass(type(blob), ctypes.Array):
            assert len(blob) % 4 == 0
            ty = ctypes.POINTER(ctypes.c_uint32 * (len(blob) // 4))
            ints = ctypes.cast(blob, ty)
            ints = [i for i in ints.contents]
        elif isinstance(blob, bytes):
            chunks = (blob[i:i+4] for i in range(0, len(blob), 4))
            dwords = [int.from_bytes(c, byteorder="little") for c in chunks]
            ints = Hwconfig.blob_to_ints(dwords)
        else:
            assert isinstance(blob, list)
            ints = [HwconfigTypes.to_value(i) for i in blob]
        return ints

    @staticmethod
    def decode_blob(hwconfig_blob):
        hwconfig_ints = Hwconfig.blob_to_ints(hwconfig_blob)
        hwconfig = []
        it = iter(hwconfig_ints)
        for key in it:
            length = next(it)
            assert length > 0
            data = [next(it) for i in range(length)]
            # if len(data) == 1: data = data[0]
            key = HwconfigTypes.to_value(key)
            hwconfig.append(HwconfigItem(key, data))
        return hwconfig

    def for_json(self):
        return Hwconfig.decode_blob(self._bytes)

    def to_ints(self):
        return Hwconfig.blob_to_ints(self._bytes)

    def for_mako(self):
        decoded = Hwconfig.decode_blob(self._bytes)
        of_interest = ((h.name.replace("INTEL_HWCONFIG_", ""), h)
                       for h in decoded)
        of_interest = (h for h in of_interest
                       if h[0] in mesa_hwconfigs_of_interest)
        of_interest = sorted([(h[0], h[1].value) for h in of_interest])
        return of_interest

    @staticmethod
    def encode_blob(decoded_blob):
        assert isinstance(decoded_blob, list), f"{type(decoded_blob)}"
        assert all(isinstance(i, HwconfigItem) for i in decoded_blob)
        # assert all(len(i) == 1 for i in decoded_blob)
        encoded = []
        for item in decoded_blob:
            encoded += item.to_dwords()
        return encoded

    @staticmethod
    def from_bytes(hwconfig_bytes):
        try:
            return Hwconfig(hwconfig_bytes)
        except Exception:
            return None

    def __lt__(self, other):
        return self._bytes < other._bytes


class Hwconfigs:

    def __init__(self):
        self.hwconfigs = {}
        self.dev_to_hwconfig = {}

    def add_device(self, hwconfig_bytes, pci_name, kernel):
        if not hwconfig_bytes:
            return None

        hwconfig = self.hwconfigs.get(hwconfig_bytes)
        if not hwconfig:
            hwconfig = Hwconfig.from_bytes(hwconfig_bytes)
            if hwconfig:
                self.hwconfigs[hwconfig_bytes] = hwconfig
        assert hwconfig

        dev_info = {
            "kernel": kernel,
            "hwconfig": hwconfig,
        }
        self.dev_to_hwconfig[pci_name] = dev_info
        return hwconfig

    def has_pci_name(self, pci_name):
        return pci_name in self.dev_to_hwconfig

    def hwconfig_for_dev(self, pci_name):
        return self.dev_to_hwconfig[pci_name]["hwconfig"]

    def kernel_for_dev(self, pci_name):
        kstr = self.dev_to_hwconfig[pci_name]["kernel"]
        return tuple(int(v) for v in kstr.split("."))

    def for_json(self):
        result = []
        d_to_h = self.dev_to_hwconfig
        for hwconfig in self.hwconfigs.values():
            devices = (pci_name for pci_name in d_to_h
                       if d_to_h[pci_name]["hwconfig"] == hwconfig)
            devices = list(sorted(f"{n}, linux-{d_to_h[n]['kernel']}"
                                  for n in devices))
            if len(devices) == 0:
                continue
            result.append({
                "devices": devices,
                "hwconfig": hwconfig,
            })
        result.sort(key=lambda d: d["hwconfig"])
        result = [{"devices": r["devices"],
                   "hwconfig_blob": r["hwconfig"].for_json(), }
                  for r in result]
        return result

    def for_mako(self):
        mako_hwconfigs = list({h["hwconfig"]
                               for h in self.dev_to_hwconfig.values()})
        hwc_to_idx = {e[1]: e[0] for e in enumerate(mako_hwconfigs)}
        mako_hwconfigs = [h.for_mako() for h in mako_hwconfigs]
        mako_dev = [(pci, list(pci.split(":")))
                    for pci in self.dev_to_hwconfig]
        mako_dev = list(sorted((int(p[1][1], 0), int(p[1][2], 0),
                                self.dev_to_hwconfig[p[0]]["hwconfig"])
                               for p in mako_dev))
        mako_dev = [{"dev_id": dev[0],
                     "rev_id": dev[1],
                     "hwconfig_idx": hwc_to_idx[dev[2]]}
                    for dev in mako_dev]
        return {
            "hwconfigs": mako_hwconfigs,
            "devices": mako_dev,
        }


class HwconfigJson:

    def __init__(self, **kw):
        self.builtin = json.JSONEncoder()
        self.indent = 0

    def dump(self, o, f):
        strs = []
        self.__encode(strs, o, 0)
        strs.append("\n")
        for s in strs:
            f.write(s)

    def dumps(self, o):
        strs = []
        self.__encode(strs, o, 0)
        return "".join(strs)

    def __encode(self, strs, o, level=0):
        def nl_indent(level): return "\n" + " " * 4 * level
        first = True
        if isinstance(o, HwconfigItem):
            for v in o.for_humans():
                if not first:
                    strs.append(", ")
                self.__encode(strs, v, level)
                first = False
        elif isinstance(o, dict):
            def blob_last(key): return (key == "hwconfig_blob", key)
            strs.append("{")
            for k in sorted(o.keys(), key=blob_last):
                if not first:
                    strs.append(",")
                strs.append(nl_indent(level + 1))
                first = False
                self.__encode(strs, str(k), level + 1)
                strs.append(": ")
                self.__encode(strs, o[k], level + 1)
            strs += [nl_indent(level), "}"]
        elif isinstance(o, (tuple, list)):
            strs.append("[")
            for i in o:
                if not first:
                    strs.append(",")
                strs.append(nl_indent(level + 1))
                first = False
                self.__encode(strs, i, level + 1)
            strs += [nl_indent(level), "]"]
        else:
            strs.append(self.builtin.encode(o))


class DrmDevice:

    def __init__(self, devpath, args):
        assert devpath.is_char_device()
        assert os.access(devpath, os.R_OK | os.W_OK)
        self.path = devpath
        stat = os.stat(devpath)
        self.major = os.major(stat.st_rdev)
        self.minor = os.minor(stat.st_rdev)
        syspath = f"/sys/dev/char/{self.major}:{self.minor}/device"
        syspath = pathlib.Path(syspath)
        assert (syspath / "drm").exists()
        self.vendor = int((syspath / "vendor").read_text().strip(), base=0)
        self.device = int((syspath / "device").read_text().strip(), base=0)
        self.pci_name = f"0x{self.vendor:x}:0x{self.device:x}"
        rev = syspath / "revision"
        if rev.exists():
            self.rev = int(rev.read_text().strip(), base=0)
            self.pci_name += f":{self.rev}"
        else:
            self.rev = None
        self.verbose = args.verbose

        self.tried_to_read_blob = False

    @staticmethod
    def get_drm_device(devpath, args):
        try:
            return DrmDevice(devpath, args)
        except Exception:
            return None

    def pci_name(self):
        return self.pci_name

    def hwconfig_bytes(self):
        if self.tried_to_read_blob:
            return self._hwconfig_bytes
        self._hwconfig_bytes = None
        self.tried_to_read_blob = True

        dev = self.path.open("wb")
        fd = dev.fileno()

        drm_ver = PyDrm.get_drm_version(fd)
        if drm_ver is None:
            return None

        if drm_ver["name"] == "xe":
            self._hwconfig_bytes = PyXeDrm.get_hwconfig(fd)
        elif drm_ver["name"] == "i915":
            self._hwconfig_bytes = PyI915Drm.get_hwconfig(fd)
        else:
            self._hwconfig_bytes = None
        fd = None
        dev.close()

        if self._hwconfig_bytes is not None and len(self._hwconfig_bytes) == 0:
            self._hwconfig_bytes = None

        return self._hwconfig_bytes

    def hwconfig_ints(self):
        return Hwconfig.blob_to_ints(self._hwconfig_bytes)


class DrmDevices:

    def __init__(self, args):
        self.args = args

        drm_devs = tuple()
        for root, dirs, files in os.walk("/dev/dri"):
            root = pathlib.Path(root)
            dirs.clear()
            drm_devs = [DrmDevice.get_drm_device(root / fn, self.args)
                        for fn in files]
            drm_devs = filter(None, drm_devs)
        self.devs = []
        self.hwconfigs = Hwconfigs()
        for drm_dev in drm_devs:
            if drm_dev.pci_name in (d.pci_name for d in self.devs):
                continue
            if drm_dev.hwconfig_bytes():
                self.devs.append(drm_dev)
                self.hwconfigs.add_device(drm_dev.hwconfig_bytes(),
                                          drm_dev.pci_name, kernel_version)
        for blob_dev in sorted(self.devs, key=lambda d: d.pci_name):
            if args.verbose:
                print("Read hwconfig info for DRM device "
                      f"{blob_dev.path} ({blob_dev.pci_name})")

    def __iter__(self):
        return iter(self.devs)

    def display(self):
        print(HwconfigJson().dumps(self.hwconfigs.for_json()))


class HwconfigDb:

    def __init__(self):
        here = pathlib.Path(sys.argv[0]).resolve().parent
        self.db_path = here / "intel_hwconfig.json"
        self.hwconfigs = Hwconfigs()
        if self.db_path.exists():
            with self.db_path.open() as f:
                db = json.load(f)
                self.__init_from_json(db)
            self.changed = False
        else:
            self.changed = True

    def __init_from_json(self, db):
        assert isinstance(db, list)
        for hwc in db:
            devices = [[s.strip() for s in d.split(",")]
                       for d in hwc["devices"]]
            hwconfig_ints = Hwconfig.blob_to_ints(hwc["hwconfig_blob"])
            hwconfig_bytes = b"".join(i.to_bytes(length=4, byteorder="little")
                                      for i in hwconfig_ints)
            for d in devices:
                kver = d[1].replace("linux-", "")
                self.hwconfigs.add_device(hwconfig_bytes, d[0], kver)

    def __contains__(self, pci_name):
        return self.hwconfigs.has_pci_name(pci_name)

    def hwconfig_for_dev(self, pci_name):
        return self.hwconfigs.hwconfig_for_dev(pci_name)

    def kernel_for_dev(self, pci_name):
        return self.hwconfigs.kernel_for_dev(pci_name)

    def add_device(self, hwconfig_bytes, pci_name, kernel):
        self.hwconfigs.add_device(hwconfig_bytes, pci_name, kernel)
        self.changed = True

    def save(self, force=False):
        if not self.changed and not force:
            return
        with self.db_path.open("w") as f:
            HwconfigJson().dump(self.hwconfigs.for_json(), f)
        self.changed = False

    def display(self):
        print(HwconfigJson().dumps(self.db))

    def for_mako(self):
        return self.hwconfigs.for_mako()


C_TEMPLATE = """\
/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Autogenerated file, do not edit!
 */

#include <stdlib.h>
#include "intel_hwconfig_gen.h"
#include "util/macros.h"

const char *
intel_hwconfig_type_to_name(enum intel_hwconfig hwconfig_type)
{
   switch (hwconfig_type) {
% for t in types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}

const char *
intel_hwconfig_mem_type_to_name(enum intel_hwconfig_mem_type mem_type)
{
   switch (mem_type) {
% for t in mem_types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}

const char *
intel_hwconfig_cache_type_to_name(enum intel_hwconfig_cache_type cache_type)
{
   switch (cache_type) {
% for t in cache_types:
   case ${t.value}: return "${t.name}";
% endfor
   default: return NULL;
   }
}

static int
intel_mesa_hwconfig_index(enum intel_hwconfig type)
{
   switch (type) {
% for e in enumerate(of_interest):
   case INTEL_HWCONFIG_${e[1]}:
      return ${e[0]};
% endfor
   default:
      return -1;
   }
}

bool
intel_mesa_hwconfig_use_item(struct intel_mesa_hwconfig *dest,
                             enum intel_hwconfig type, uint32_t len,
                             const uint32_t *values)
{
   if (len != 1)
      return false;

   int index = intel_mesa_hwconfig_index(type);
   assert(index < (int)ARRAY_SIZE(dest->is_valid.array));
   if (index < 0)
      return false;

   dest->is_valid.array[index] = true;
   dest->value.array[index] = values[0];
   return true;
}

int
intel_mesa_hwconfig_copy(struct intel_mesa_hwconfig *dest,
                         const struct intel_mesa_hwconfig *src,
                         bool only_missing)
{
   static_assert(sizeof(dest->is_valid.array) == ${len(of_interest)});
% for e in enumerate(of_interest):
   static_assert(offsetof(struct intel_mesa_hwconfig, is_valid.array[${e[0]}]) ==
                 offsetof(struct intel_mesa_hwconfig, is_valid.${e[1]}));
   static_assert(offsetof(struct intel_mesa_hwconfig, value.array[${e[0]}]) ==
                 offsetof(struct intel_mesa_hwconfig, value.${e[1]}));
% endfor

   int updated = 0;
   int i;
   for (i = 0; i < ${len(of_interest)}; i++) {
      if (src->is_valid.array[i] &&
          (!only_missing || !dest->is_valid.array[i])) {
         dest->is_valid.array[i] = true;
         dest->value.array[i] = src->value.array[i];
         updated++;
      }
   }
   return updated;
}

#define SET_EMBEDDED_VALUE(n, v) .is_valid.n = true, .value.n = v

static const struct intel_mesa_hwconfig
embedded_hwconfigs[] = {
% for eh in embedded_hwconfig["hwconfigs"]:
   {
% for eh_field in eh:
      SET_EMBEDDED_VALUE(${eh_field[0]}, ${eh_field[1]}),
% endfor
   },
% endfor
};

struct embedded_device {
   uint16_t dev_id;
   uint8_t rev_id;
   uint16_t hwconfig_idx;
};

static const struct embedded_device
embedded_devices[] = {
% for dev in embedded_hwconfig["devices"]:
   {
      .dev_id = ${f"0x{dev['dev_id']:04x}"},
      .rev_id = ${dev["rev_id"]},
      .hwconfig_idx = ${dev["hwconfig_idx"]},
   },
% endfor
};

const struct intel_mesa_hwconfig *
intel_get_mesa_embedded_hwconfig(uint16_t dev_id, uint8_t rev_id)
{
   const struct embedded_device *dev = &embedded_devices[0];
   const struct embedded_device *end_dev = dev + ARRAY_SIZE(embedded_devices);
   for ( ; dev < end_dev; dev++) {
      if (dev->dev_id == dev_id && dev->rev_id == rev_id) {
         assert(dev->hwconfig_idx < ARRAY_SIZE(embedded_hwconfigs));
         return &embedded_hwconfigs[dev->hwconfig_idx];
      }

   }
   return NULL;
}
"""


H_TEMPLATE = """\
/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Autogenerated file, do not edit!
 */

#ifndef _INTEL_HWCONFIG_GEN_H_
#define _INTEL_HWCONFIG_GEN_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum intel_hwconfig {
% for t in types:
   ${t.name} = ${t.value},
% endfor
};

enum intel_hwconfig_mem_type {
% for t in mem_types:
   ${t.name} = ${t.value},
% endfor
};

enum intel_hwconfig_cache_type {
% for t in cache_types:
   ${t.name} = ${t.value},
% endfor
};

struct intel_mesa_hwconfig {
   union {
      uint32_t array[${len(of_interest)}];
      struct {
% for i in of_interest:
         uint32_t ${i};
% endfor
      };
   } value;
   union {
      bool array[${len(of_interest)}];
      struct {
% for i in of_interest:
         bool ${i};
% endfor
      };
   } is_valid;
};

const char *
intel_hwconfig_type_to_name(enum intel_hwconfig);

const char *
intel_hwconfig_mem_type_to_name(enum intel_hwconfig_mem_type);

const char *
intel_hwconfig_cache_type_to_name(enum intel_hwconfig_cache_type);

bool
intel_mesa_hwconfig_use_item(struct intel_mesa_hwconfig *dest,
                             enum intel_hwconfig type, uint32_t len,
                             const uint32_t *values);

int
intel_mesa_hwconfig_copy(struct intel_mesa_hwconfig *dest,
                         const struct intel_mesa_hwconfig *src,
                         bool only_missing);

const struct intel_mesa_hwconfig *
intel_get_mesa_embedded_hwconfig(uint16_t dev_id, uint8_t rev_id);

#ifdef __cplusplus
}
#endif

#endif /* _INTEL_HWCONFIG_GEN_H_ */
"""


class GenHwconfigSources:

    def __init__(self, args, db):
        self.args = args
        self.db = db
        self.template_input = {
            "types": HwconfigTypes,
            "mem_types": HwconfigMemTypes,
            "cache_types": HwconfigCacheTypes,
            "of_interest": mesa_hwconfigs_of_interest,
        }
        import mako.template
        self.c_template = mako.template.Template(C_TEMPLATE)
        self.h_template = mako.template.Template(H_TEMPLATE)

    def gen(self):
        gen_count = 0
        if self.gen_c_source():
            gen_count += 1
        if self.gen_h_source():
            gen_count += 1
        if gen_count == 0:
            print("info: No sources specified to be generated")
        return gen_count > 0

    def __update_template_input(self):
        self.template_input["embedded_hwconfig"] = self.db.for_mako()

    def gen_c_source(self):
        if self.args.c is None:
            return False
        self.__update_template_input()
        with open(self.args.c, 'w', encoding='utf8') as c:
            c.write(self.c_template.render(**self.template_input))
        return True

    def gen_h_source(self):
        if self.args.h is None:
            return False
        self.__update_template_input()
        with open(self.args.h, 'w', encoding='utf8') as h:
            h.write(self.h_template.render(**self.template_input))
        return True


class HwconfigApp:

    def __init__(self):
        self.parse_args()
        mode = self.args.mode if self.args.mode else "display"
        if mode == "check-db":
            ok = True
            self.drm_devs = DrmDevices(self.args)
            self.db = HwconfigDb()
            for d in self.drm_devs:
                if d.pci_name not in self.db:
                    ok = False
                    print("error: use update-db to add "
                          f"{d.pci_name} to hwconfig database.",
                          file=sys.stderr)
                    continue
                db_ver = self.db.kernel_for_dev(d.pci_name)
                if kernel_version_tuple < db_ver and not self.verbose:
                    continue
                dev_ints = d.hwconfig_ints()
                db_ints = self.db.hwconfig_for_dev(d.pci_name).to_ints()
                if dev_ints == db_ints:
                    if self.verbose:
                        print(f"info: {d.pci_name} hwconfig blob from kernel "
                              "matched database.")
                    continue
                if kernel_version_tuple >= db_ver:
                    ok = False
                    print("error: use update-db to update "
                          f"{d.pci_name} in hwconfig database.",
                          file=sys.stderr)
                else:
                    assert self.verbose
                    print(f"info: {d.pci_name} hwconfig blob mismatch, but "
                          "database info is newer.")
            sys.exit(0 if ok else 1)
        elif mode == "gen-sources":
            self.db = HwconfigDb()
            self.gen_sources = GenHwconfigSources(self.args, self.db)
            sys.exit(0 if self.gen_sources.gen() else 1)
        elif mode == "display":
            self.drm_devs = DrmDevices(self.args)
            self.drm_devs.display()
        elif mode == "update-db":
            added = 0
            modified = 0
            self.drm_devs = DrmDevices(self.args)
            self.db = HwconfigDb()
            for d in self.drm_devs:
                if d.pci_name not in self.db:
                    print(f"{d.pci_name} device does not exist in current "
                          "hwconfig database.")
                    print("Only shipping production devices should be added "
                          "to the hwconfig database!")
                    print("Confirm that this device is available for purchase "
                          "publicly.")
                    confirmation = input("Is this a production device? "
                                         "(enter YES to confirm) ")
                    if confirmation != "YES":
                        print(f"Skipping adding new {d.pci_name} device!")
                        continue

                    print(f"Adding {d.pci_name} to hwconfig database.")
                    added += 1
                    self.db.add_device(d.hwconfig_bytes(), d.pci_name,
                                       kernel_version)
                    continue

                dev_ints = d.hwconfig_ints()
                db_ints = self.db.hwconfig_for_dev(d.pci_name).to_ints()
                if dev_ints == db_ints:
                    if self.verbose:
                        print(f"info: {d.pci_name} hwconfig blob from kernel "
                              "matched database.")
                    continue

                db_ver = self.db.kernel_for_dev(d.pci_name)
                if kernel_version_tuple < db_ver:
                    if self.verbose:
                        print(f"info: {d.pci_name} hwconfig blob mismatch, but "
                              "database info is newer.")
                    continue

                modified += 1
                self.db.add_device(d.hwconfig_bytes(), d.pci_name,
                                   kernel_version)
            if added or modified or self.args.rewrite:
                self.db.save(force=self.args.rewrite)
            if added or modified:
                print("Updated hwconfig database: "
                      f"{added} added, {modified} modified.")
                if added > 0:
                    print("\nInclude a reference to the product web page for "
                          "added devices\nin the commit message!")
        else:
            assert False, f"Mode of {mode} is not supported"

    def parse_args(self):
        p = argparse.ArgumentParser()
        p.add_argument("-v", "--verbose", action="count", default=0,
                       help="Enable verbose output")

        sps = p.add_subparsers(dest="mode", help="Specifies the run mode")

        sp = sps.add_parser("check-db")

        sp = sps.add_parser("display")

        sp = sps.add_parser("gen-sources")
        sp.add_argument("--h", help="Generate C header file")
        sp.add_argument("--c", help="Generate C source file")

        sp = sps.add_parser("update-db")
        sp.add_argument("--rewrite", action="store_true", default=False,
                        help="Force rewrite of db file")

        a = p.parse_args()

        self.args = a
        self.verbose = a.verbose


if __name__ == "__main__":
    HwconfigApp()
