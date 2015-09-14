#import "MWMSearchShowOnMapCell.h"

@implementation MWMSearchShowOnMapCell

- (void)awakeFromNib
{
  CALayer * sl = self.layer;
  sl.shouldRasterize = YES;
  sl.rasterizationScale = UIScreen.mainScreen.scale;
}

+ (CGFloat)cellHeight
{
  return 44.0;
}

@end
