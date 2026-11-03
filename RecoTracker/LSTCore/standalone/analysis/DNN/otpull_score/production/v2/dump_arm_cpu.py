# otpull v2 dump arm (lane itaca_extoff/w2/integrate, cfg/cand2_menu_cpu.py; this is its branch form, the same process):
# the extension-off menu (lane itaca_extoff/port cfg/op_cpu.py = a 75e33_timing CPU config + customizeOP) with customizeOP
# replaced by the candidate policy, the v2 classifier tables and the v1 otpull table (the WP does not change the dumped set).
import os
os.environ.setdefault('PTT_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'pixel_track_torch'))
# exec(<menu config>) here, then:
exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'policy_cand.py')).read())
customizeCandidate(process, pttWP=os.path.join(os.environ['PTT_DIR'], 'shipped', 'v2', 'wp_eps4_0.99_fwd0.995_eps3_0.99.json'),
                   otpullWP=[0.99722, 0.993477, 0.998458, 0.996205, 0.99574, 0.999811, 0.992345, 0.970976, 0.993204, 0.987212,
                             0.96039, 0.987212, 0.998083, 0.988477, 0.998083, 0.998083, 0.988477, 0.998083])
