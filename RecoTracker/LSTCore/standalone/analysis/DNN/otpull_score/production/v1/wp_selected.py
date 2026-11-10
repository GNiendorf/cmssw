# otpull pair network v1: the working-point table chosen by the end-to-end MTV scan (lane itaca_extoff/w2/f4, 2026-11-03):
# q = 0.6 / 0.8 / 0.6 true-pair keep in the T3 |eta| columns [0, 0.9) / [0.9, 1.5) / [1.5, inf), floor = the chi2
# decision's keep per cell (wp_otpull.py; table key "0.6/0.8/0.6" of otpull_v1.json). Row-major (6 T3-pT rows with edges
# 2/5/10/25/50 GeV x 3 |eta| columns). Use: customizeOtpullScore(process, wp=OTPULL_WP_V1)
OTPULL_WP_V1 = [0.99722, 0.993477, 0.998458, 0.996205, 0.99574, 0.999811, 0.992345, 0.970976, 0.993204, 0.987212, 0.96039, 0.987212, 0.998083, 0.988477, 0.998083, 0.998083, 0.988477, 0.998083]
