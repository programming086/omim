package com.mapswithme.maps.widget;

import android.app.Activity;
import android.support.annotation.StringRes;
import android.support.v7.widget.Toolbar;
import android.view.View;
import com.mapswithme.maps.R;
import com.mapswithme.util.UiUtils;
import com.mapswithme.util.Utils;

public class ToolbarController
{
  protected final Activity mActivity;
  protected final Toolbar mToolbar;

  public ToolbarController(View root, Activity activity)
  {
    mActivity = activity;
    mToolbar = (Toolbar) root.findViewById(R.id.toolbar);
    UiUtils.showHomeUpButton(mToolbar);
    mToolbar.setNavigationOnClickListener(new View.OnClickListener()
    {
      @Override
      public void onClick(View v)
      {
        onUpClick();
      }
    });
  }

  public void onUpClick()
  {
    Utils.navigateToParent(mActivity);
  }

  public ToolbarController setTitle(CharSequence title)
  {
    mToolbar.setTitle(title);
    return this;
  }

  public ToolbarController setTitle(@StringRes int title)
  {
    mToolbar.setTitle(title);
    return this;
  }

  public Toolbar getToolbar()
  {
    return mToolbar;
  }
}
