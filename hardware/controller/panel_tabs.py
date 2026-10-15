"""KiKit tabs plugin for the controller card panel.

KiKit's fixed tabs are spaced evenly along each board edge. On this card, any
count above one puts a tab under the RM2 module, which hangs about 4.8 mm past
the board's lower edge (module span 25.2-39.7 mm from the left of the board's
bounding box). This plugin places the tabs at the offsets listed in the preset's
tabs.arg instead, measured from the left of each board's bounding box, on both
long edges. Offsets below about 9 mm would land on the card-edge fingers.
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
