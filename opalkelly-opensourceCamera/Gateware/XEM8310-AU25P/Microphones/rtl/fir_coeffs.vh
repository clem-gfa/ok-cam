// Genere par sim/gen_fir_coeffs.py : FIR symetrique de 73 coefficients, 37 uniques, Q17.
// Somme des 73 coefficients = 131070 (1.0 = 131072)
function signed [17:0] fir_coef(input [5:0] k);
  case (k)
    6'd0: fir_coef = 18'sd0;
    6'd1: fir_coef = 18'sd0;
    6'd2: fir_coef = 18'sd0;
    6'd3: fir_coef = 18'sd0;
    6'd4: fir_coef = 18'sd0;
    6'd5: fir_coef = 18'sd4;
    6'd6: fir_coef = -18'sd4;
    6'd7: fir_coef = -18'sd17;
    6'd8: fir_coef = 18'sd19;
    6'd9: fir_coef = 18'sd40;
    6'd10: fir_coef = -18'sd56;
    6'd11: fir_coef = -18'sd71;
    6'd12: fir_coef = 18'sd131;
    6'd13: fir_coef = 18'sd102;
    6'd14: fir_coef = -18'sd264;
    6'd15: fir_coef = -18'sd116;
    6'd16: fir_coef = 18'sd474;
    6'd17: fir_coef = 18'sd83;
    6'd18: fir_coef = -18'sd779;
    6'd19: fir_coef = 18'sd43;
    6'd20: fir_coef = 18'sd1187;
    6'd21: fir_coef = -18'sd325;
    6'd22: fir_coef = -18'sd1695;
    6'd23: fir_coef = 18'sd842;
    6'd24: fir_coef = 18'sd2288;
    6'd25: fir_coef = -18'sd1706;
    6'd26: fir_coef = -18'sd2933;
    6'd27: fir_coef = 18'sd3084;
    6'd28: fir_coef = 18'sd3592;
    6'd29: fir_coef = -18'sd5297;
    6'd30: fir_coef = -18'sd4216;
    6'd31: fir_coef = 18'sd9304;
    6'd32: fir_coef = 18'sd5354;
    6'd33: fir_coef = -18'sd17540;
    6'd34: fir_coef = -18'sd9611;
    6'd35: fir_coef = 18'sd44337;
    6'd36: fir_coef = 18'sd78562;
    default: fir_coef = 18'sd0;
  endcase
endfunction
