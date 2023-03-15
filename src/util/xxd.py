# encoding=utf-8
# Copyright © 2018 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Converts a file to a C/C++ #include containing a string

import argparse
import io
import os
import sys


def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', help="Name of output file")
    parser.add_argument('input', nargs='*', help="Name of input file(s)")
    parser.add_argument("-n", "--name-and-input", nargs=2, action='append',
                        default=[], metavar='PARAM',
                        help="Name of C variable, then input file")
    parser.add_argument("-b", "--binary", dest='binary', action='store_const',
                        const=True, default=False)
    args = parser.parse_args()
    return args


def filename_to_C_identifier(n):
    if n[0] != '_' and not n[0].isalpha():
        n = "_" + n[1:]

    return "".join([c if c.isalnum() or c == "_" else "_" for c in n])


def emit_byte(f, b):
    f.write("0x{:02x}, ".format(ord(b)).encode('utf-8'))


def process_file(args):
    # For input files specified without a symbol name, pick one based
    # on thename of the input file.
    sym_input = [(filename_to_C_identifier(fn), fn) for fn in args.input]
    # Add the input files that specified a symbol name.
    sym_input.extend([(nf[0], nf[1]) for nf in args.name_and_input])
    try:
        with io.open(args.output, "wb") as outfile:
            for (name, f) in sym_input:
                with io.open(f, "rb") as infile:
                        outfile.write("static const char {}[] = \n".format(name).encode('utf-8'))
                        outfile.write(b"{")

                        linecount = 0
                        while True:
                            byte = infile.read(1)
                            if byte == b"":
                                break

                            if not args.binary:
                                assert(ord(byte) != 0)

                            emit_byte(outfile, byte)
                            linecount = linecount + 1
                            if linecount > 20:
                                outfile.write(b"\n ")
                                linecount = 0

                        if not args.binary:
                            outfile.write(b"\n0")
                        outfile.write(b"\n};\n\n")
    except Exception:
        # In the event that anything goes wrong, delete the output file,
        # then re-raise the exception. Deleteing the output file should
        # ensure that the build system doesn't try to use the stale,
        # half-generated file.
        os.unlink(args.output)
        raise


def main():
    args = get_args()
    process_file(args)


if __name__ == "__main__":
    main()
