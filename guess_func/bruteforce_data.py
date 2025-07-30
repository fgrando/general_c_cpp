from cffi import FFI
import numpy as np
import os
import csv

# pip install scikit-learn pandas joblib cffi


ffi = FFI()

# Define the C function signature
ffi.cdef("""
    void blackbox_function(double a, double b, double c, double d, double* out);
""")

# Load the DLL
dll_path = os.path.abspath("x64/Debug/guess_func.dll")
C = ffi.dlopen(dll_path)

# Allocate memory for output
output = ffi.new("double[4]")

csv_filename = "calculation_data.csv"
header = ['a', 'b', 'c', 'd', 'out1', 'out2', 'out3', 'out4']

with open(csv_filename, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(header)

    # Sweep over ranges of sensor values
    a_set = np.linspace(0, 20, 21)      # 0 to 20 meters
    b_set = np.linspace(-0.5, 0.5, 11)  # -0.5 to 0.5 radians
    c_set = np.linspace(-0.5, 0.5, 11)
    d_set = np.linspace(-0.5, 0.5, 11)

    for a in a_set:
        for b in b_set:
            for c in c_set:
                for d in d_set:
                    output = ffi.new("double[4]")
                    C.blackbox_function(a, b, c, d, output)

                    row = [a, b, c, d] + [output[i] for i in range(4)]
                    writer.writerow(row)

print(f"Data saved to {csv_filename}")
