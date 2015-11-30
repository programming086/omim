package com.mapswithme.maps.routing;

import android.view.View;
import android.widget.Button;

import com.mapswithme.maps.MwmActivity;
import com.mapswithme.maps.R;
import com.mapswithme.maps.bookmarks.data.MapObject;
import com.mapswithme.util.UiUtils;
import com.mapswithme.util.statistics.AlohaHelper;
import com.mapswithme.util.statistics.Statistics;

public class RoutingPlanInplaceController extends RoutingPlanController
{
  public RoutingPlanInplaceController(MwmActivity activity)
  {
    super(activity.findViewById(R.id.routing_plan_frame), activity);
  }

  public void show(boolean show)
  {
    if (show == UiUtils.isVisible(mFrame))
      return;

    if (show)
    {
      showSlots(!(RoutingController.get().getStartPoint() instanceof MapObject.MyPosition) || (RoutingController.get().getEndPoint() == null),
                false);
    }

    UiUtils.showIf(show, mFrame);
    if (show)
      updatePoints();
  }

  public void setStartButton()
  {
    final MwmActivity activity = (MwmActivity) mActivity;

    Button start = activity.getMainMenu().getRouteStartButton();
    RoutingController.get().setStartButton(start);
    start.setOnClickListener(new View.OnClickListener()
    {
      @Override
      public void onClick(View v)
      {
        activity.closeMenu(Statistics.EventName.ROUTING_START, AlohaHelper.ROUTING_START, new Runnable()
        {
          @Override
          public void run()
          {
            RoutingController.get().start();
          }
        });
      }
    });
  }
}
