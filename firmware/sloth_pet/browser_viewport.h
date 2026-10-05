#pragma once
// Logical 240x240 coordinates, doubled by the panel transport. Keep controls
// clear of the physical rounded corners and leave a 3-pixel reading gutter.
#define BROWSER_PAGE_TOP 32u
#define BROWSER_PAGE_BOTTOM 208u
#define BROWSER_PAGE_MARGIN 3u
#define BROWSER_VIEW_HEIGHT ((BROWSER_PAGE_BOTTOM - BROWSER_PAGE_TOP) * 2u)
#define BROWSER_VIEW_WIDTH ((240u - BROWSER_PAGE_MARGIN * 2u) * 2u)
