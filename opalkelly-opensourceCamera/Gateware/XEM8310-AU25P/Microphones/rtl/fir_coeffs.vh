// Genere par sim/gen_fir_coeffs.py : FIR symetrique de 73 coefficients, 37 uniques, Q17.
// Somme des 73 coefficients = 131072 (1.0 = 131072)
function signed [17:0] fir_coef(input [5:0] k);
  case (k)
    6'd0: fir_coef = 18'sd0;
    6'd1: fir_coef = 18'sd0;
    6'd2: fir_coef = 18'sd0;
    6'd3: fir_coef = 18'sd0;
    6'd4: fir_coef = 18'sd0;
    6'd5: fir_coef = -18'sd4;
    6'd6: fir_coef = -18'sd1;
    6'd7: fir_coef = 18'sd19;
    6'd8: fir_coef = 18'sd1;
    6'd9: fir_coef = -18'sd50;
    6'd10: fir_coef = -18'sd3;
    6'd11: fir_coef = 18'sd108;
    6'd12: fir_coef = 18'sd4;
    6'd13: fir_coef = -18'sd207;
    6'd14: fir_coef = -18'sd7;
    6'd15: fir_coef = 18'sd362;
    6'd16: fir_coef = 18'sd11;
    6'd17: fir_coef = -18'sd595;
    6'd18: fir_coef = -18'sd15;
    6'd19: fir_coef = 18'sd933;
    6'd20: fir_coef = 18'sd22;
    6'd21: fir_coef = -18'sd1408;
    6'd22: fir_coef = -18'sd30;
    6'd23: fir_coef = 18'sd2070;
    6'd24: fir_coef = 18'sd42;
    6'd25: fir_coef = -18'sd2994;
    6'd26: fir_coef = -18'sd59;
    6'd27: fir_coef = 18'sd4315;
    6'd28: fir_coef = 18'sd85;
    6'd29: fir_coef = -18'sd6330;
    6'd30: fir_coef = -18'sd120;
    6'd31: fir_coef = 18'sd9958;
    6'd32: fir_coef = 18'sd649;
    6'd33: fir_coef = -18'sd17765;
    6'd34: fir_coef = -18'sd4294;
    6'd35: fir_coef = 18'sd44357;
    6'd36: fir_coef = 18'sd72964;
    default: fir_coef = 18'sd0;
  endcase
endfunction
