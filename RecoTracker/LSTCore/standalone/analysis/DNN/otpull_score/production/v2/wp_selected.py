# otpull pair network v2 (retrained on the integrated candidate, lane itaca_extoff/w2/integrate, 2026-11-03): the working-point
# table with v1's MTV-chosen rule, q = 0.6 / 0.8 / 0.6 true-pair keep in the T3 |eta| columns [0, 0.9) / [0.9, 1.5) /
# [1.5, inf), floor = the chi2 decision's keep per cell (wp_otpull.py; table key "0.6/0.8/0.6" of otpull_v2_wp.json).
# Row-major (6 T3-pT rows with edges 2/5/10/25/50 GeV x 3 |eta| columns). Use: customizeOtpullScore(process, wp=OTPULL_WP_V2)
OTPULL_WP_V2 = [0.997263, 0.992341, 0.998056, 0.996803, 0.987669, 0.999579, 0.992267, 0.874658, 0.995989, 0.997754, 0.987784, 0.997754, 0.997754, 0.987784, 0.997754, 0.997754, 0.987784, 0.997754]
