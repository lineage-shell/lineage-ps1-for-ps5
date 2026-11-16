"""Writes the showcase clips' preview scripts (tools/ui-preview/showcase/clip*.txt): deliberate moves with pauses.
SPDX-License-Identifier: GPL-3.0-or-later"""
import os
here = os.path.dirname(os.path.abspath(__file__))
KEYS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

class S:
    def __init__(s, title): s.l = ['# ' + title]
    def w(s, n): s.l.append('wait %d' % n); return s
    def p(s, b, pause=24): s.l.append('press ' + b); return s.w(pause)
    def ps(s, b, n, pause=24):
        for _ in range(n): s.p(b, pause)
        return s
    def type(s, word, pause=14):
        cur = 0
        for ch in word:
            k = KEYS.index(ch); r, c = divmod(k, 7); r0, c0 = divmod(cur, 7)
            for m in (['down'] * (r - r0) if r > r0 else ['up'] * (r0 - r)) + (['right'] * (c - c0) if c > c0 else ['left'] * (c0 - c)):
                s.p(m, pause)
            s.p('cross', pause + 10); cur = k
        return s
    def save(s, name):
        s.l.append('quit'); open(os.path.join(here, name), 'w').write('\n'.join(s.l) + '\n')

S('clip 2: main menu (startup_intro=0)').w(90).p('down', 45).p('up', 45).p('down', 45).p('up', 40).p('cross', 140).save('clip2-menu.txt')

c = S('clip 3: library browse, paging, filter, sort, search (startup_intro=0)').w(70).p('cross', 130)
c.ps('right', 3, 20).p('down', 22).ps('right', 6, 18).w(20).p('right', 50).p('left', 50)
c.p('r1', 45).p('r1', 45).p('l1', 45).p('l1', 40)
c.p('l2', 50).p('l2', 50).p('l2', 50)
c.p('r2', 45).p('r2', 45).p('r2', 45).p('r2', 45)
c.p('triangle', 40).type('SPY').w(50).p('circle', 70).ps('right', 2, 30).w(40).save('clip3-library.txt')

S('clip 4: library, 6x2 or 4:3 (set shelf_layout / shelf_shape first)').w(70).p('cross', 130).ps('right', 3, 22).p('down', 22).ps('right', 2, 22).p('r1', 50).p('l1', 50).w(30).save('clip4-layout.txt')

c = S('clip 5: game page (Final Fantasy VII, 3 discs, saves)').w(70).p('cross', 120).p('triangle', 30).type('VII').w(30).p('circle', 50).p('cross', 110)
c.p('down', 30).p('cross', 50).p('down', 30).p('down', 30).ps('up', 3, 25)
c.p('right', 40).p('right', 30).p('down', 30).p('left', 30).p('square', 60).p('cross', 50)
c.ps('left', 3, 25).ps('down', 4, 25).p('right', 30).p('right', 30).p('cross', 50).p('cross', 50).p('cross', 50)
c.p('down', 30).p('right', 30).p('right', 40).p('cross', 100).save('clip5-game-page.txt')

c = S('clip 6: per-game settings, global settings').w(70).p('cross', 120).p('triangle', 30).type('SILENT').w(20).p('circle', 50).p('cross', 110)
c.p('down', 25).p('down', 25).p('cross', 90).p('down', 30).p('down', 30).p('right', 45).p('square', 45).p('r1', 45).p('r1', 45).p('l1', 40)
c.l.append('hold down 70'); c.w(40).p('cross', 50).p('right', 30).p('left', 30).p('circle', 50).p('circle', 90)
c.p('circle', 50).p('circle', 70).p('down', 30).p('cross', 120).ps('r1', 3, 45).p('down', 30).p('right', 50).p('left', 50).p('circle', 80).save('clip6-settings.txt')

c = S('clip 7: in game (SwanStationPS5_STUB_MOVIE: a recording of Crash Bandicoot), menu, save, back').w(70).p('cross', 120)
c.p('triangle', 30).type('CRASHB').w(20).p('circle', 50).p('cross', 110).p('cross', 600)
c.l.append('hold l3+r3 50'); c.w(70).p('down', 40).p('r1', 30).p('r1', 40).p('cross', 120)
c.l.append('hold l3+r3 50'); c.w(70)
c.ps('down', 6, 20).w(20).p('cross', 150).p('right', 60).w(60).save('clip7-ingame.txt')
