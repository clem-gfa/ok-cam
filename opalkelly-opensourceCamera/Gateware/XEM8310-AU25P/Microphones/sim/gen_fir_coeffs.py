#!/usr/bin/env python3
"""Genere ../rtl/fir_coeffs.vh : les 37 coefficients Q17 (18 bits signes) du FIR symetrique."""
import pdm_chain as m
c = m.fir_q17()
tot = int(2 * c[:36].sum() + c[36])
with open("../rtl/fir_coeffs.vh", "w") as f:
    f.write("// Genere par sim/gen_fir_coeffs.py : FIR symetrique de 73 coefficients, 37 uniques, Q17.\n")
    f.write("// Somme des 73 coefficients = %d (1.0 = %d)\n" % (tot, 1 << m.COEF_FRAC))
    f.write("function signed [17:0] fir_coef(input [5:0] k);\n  case (k)\n")
    for k, v in enumerate(c):
        f.write("    6'd%d: fir_coef = %s18'sd%d;\n" % (k, "-" if v < 0 else "", abs(int(v))))
    f.write("    default: fir_coef = 18'sd0;\n  endcase\nendfunction\n")
print("somme =", tot, "max |coef| =", int(abs(c).max()), "limite 131071")
