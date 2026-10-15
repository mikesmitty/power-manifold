"""KiKit tabs plugin that places tabs at fixed offsets.

KiKit's fixed tabs are spaced evenly along each board edge, which puts some of
them on parts near the edge. This plugin places the tabs at the offsets listed
in the preset's tabs.arg instead, measured from the left of each board's
bounding box, on both long edges. A preset names it as
../panel_tabs.py.OffsetTabs, because panelize.sh runs KiKit from the module's
folder.

Controller card: any even spacing with more than one tab puts a tab under the
RM2 module, which hangs about 4.8 mm past the board's lower edge (module span
25.2-39.7 mm from the left of the bounding box). Offsets below about 9 mm would
land on the card-edge fingers.

Status strip: the capacitors sit 0.5 mm from the upper edge and J1 reaches
within 0.5 mm of both edges, so the tabs sit midway between the LED columns and
leave J1's span (35.5-44.4 mm) clear. Where the lower edge is notched, a tab
reaches from the notch edge to the next board.
"""

from kikit.annotations import TabAnnotation
from kikit.plugin import TabsPlugin
from kikit.units import readLength


class OffsetTabs(TabsPlugin):
    def buildTabAnnotations(self, panel):
        width = self.preset["tabs"]["vwidth"]
        offsets = [readLength(x.strip()) for x in self.userArg.split(",")]
        for s in panel.substrates:
            minx, miny, maxx, maxy = s.bounds()
            for offset in offsets:
                # The direction points into the board, as KiKit's own fixed
                # tabs do: down from the upper edge, up from the lower edge.
                s.annotations.append(
                    TabAnnotation(None, (minx + offset, miny), [0, 1], width))
                s.annotations.append(
                    TabAnnotation(None, (minx + offset, maxy), [0, -1], width))
