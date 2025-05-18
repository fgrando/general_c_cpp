#!/usr/bin/env python3

import os
import re
import sys

TRACER_MARK = "/* BACKTRACE_INSERT_PROBE_HERE */"
TRACER_START = "/* BACKTRACE_START */"
TRACER_FUNC = "BACKTRACE_PUSH"
TRACER_END = "/* BACKTRACE_END */"
TRACER_HEADER = f'#include "backtrace.h"\n'


class CCodeParser:
    def remove_comments(self, code):
        # Remove C comments from code
        code = re.sub("/\*.*?\*/", "", code)
        code = re.sub("//.*[\n]+", "", code)
        return code

    def function_blocks(self, code):
        func_bodies = []
        stack = []
        for i in range(len(code)):
            c = code[i]
            if c == "{":
                stack.append(i)
            elif c == "}":
                if len(stack) == 1:
                    func_bodies.append(code[stack[0] : i + 1])
                stack.pop()
        return func_bodies

    def functions(self, file):
        findings = []
        with open(file, "r") as f:
            orig = f.read()
        code = orig
        func_bodies = self.function_blocks(code)
        for block in func_bodies:
            code = code.replace(block, "{#}")

        func_names = re.findall("\w+\s*\([^\)]+\)\s*\{", code, re.DOTALL)
        for i in range(len(func_names)):
            func = func_names[i].replace("{", func_bodies[i])
            # print([func], code)
            # input('helow')
            if func in orig:
                findings.append(func)

        return findings

class CodeEditor:
    def __init__(self, sources):
        self.sources = sources
        self.sequence = 0
        self.parser = CCodeParser()

    def instrument(self):
        for file in self.sources:
            has_header = False
            previous = self.sequence
            result = []
            with open(file, "r") as f:
                for line in f.readlines():
                    if TRACER_MARK in line.strip():
                        code = TRACER_START
                        code += f"{TRACER_FUNC}({self.sequence});"
                        code += TRACER_END
                        code += "\n"
                        self.sequence += 1
                    else:
                        code = line
                    result.append(code)

                    if TRACER_HEADER == line:
                        has_header = True

            if previous != self.sequence:
                print(f"{file}: {self.sequence - previous} traces.")
                with open(file, "w") as f:
                    if not has_header:
                        # print(f'{file}: add {TRACER_HEADER}')
                        f.write(TRACER_HEADER)
                    f.writelines(result)

    def clean(self):
        for file in self.sources:
            previous = self.sequence
            result = []
            with open(file, "r") as f:
                for line in f.readlines():
                    if TRACER_START in line.strip() and TRACER_END in line.strip():
                        code = TRACER_MARK
                        code += "\n"
                        self.sequence -= 1
                    elif TRACER_HEADER in line.strip():
                        self.sequence -= 1
                    else:
                        code = line
                    result.append(code)
            if previous != self.sequence:
                # print(f'{file}: {self.sequence - previous} traces.')
                with open(file, "w") as f:
                    f.writelines(result)
        if self.sequence > 0:
            print(f"FAIL: {self.sequence} traces left")

    def show(self):
        for file in self.sources:
            lines = open(file, "r").readlines()
            for func in self.parser.functions(file):
                name = func.split("(")[0].strip()
                for line in func.split("\n"):
                    if TRACER_START in line.strip():
                        clean = self.parser.remove_comments(line)
                        number = lines.index(line + "\n")
                        print(f"{clean}\t{file}:{name}:{number + 1}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python backtrace_inject.py <root_directory> [instrument|clean]")
        sys.exit(1)

    # Get the root directory from the user
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    instrument = True if sys.argv[2] == "instrument" else False

    # Find all C source files in the given directory
    sources = []
    for dirpath, _, filenames in os.walk(root):
        for filename in filenames:
            if filename.endswith(".c"):
                sources.append(os.path.join(dirpath, filename))

    print(f"Found {len(sources)} C source files")
    code = CodeEditor(sources)
    if instrument:
        code.instrument()
    else:
        code.clean()
    code.show()
    print("done!")
