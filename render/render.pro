TARGET = render
TEMPLATE = lib
CONFIG += staticlib warn_on

ROOT_DIR = ..

include($$ROOT_DIR/common.pri)

INCLUDEPATH *= $$ROOT_DIR/3party/protobuf/src $$ROOT_DIR/3party/freetype/include

SOURCES += \
    $$ROOT_DIR/3party/agg/agg_curves.cpp \
    software_renderer.cpp \
    gpu_drawer.cpp \
    cpu_drawer.cpp \
    drawer.cpp \
    feature_info.cpp \
    feature_styler.cpp \
    geometry_processors.cpp \
    proto_to_styles.cpp \
    text_engine.cpp \
    default_font.cpp \
    window_handle.cpp \
    render_policy.cpp \
    route_renderer.cpp \
    route_shape.cpp \
    tiling_render_policy_st.cpp \
    tiling_render_policy_mt.cpp \
    simple_render_policy.cpp \
    queued_renderer.cpp \
    basic_tiling_render_policy.cpp \
    tiler.cpp \
    tile.cpp \
    tile_cache.cpp \
    tile_set.cpp \
    tile_renderer.cpp \
    feature_processor.cpp \
    events.cpp \
    coverage_generator.cpp \
    scales_processor.cpp \
    yopme_render_policy.cpp \


HEADERS += \
    software_renderer.hpp \
    gpu_drawer.hpp \
    cpu_drawer.hpp \
    frame_image.hpp \
    drawer.hpp \
    feature_info.hpp \
    feature_styler.hpp \
    path_info.hpp \
    area_info.hpp \
    geometry_processors.hpp \
    proto_to_styles.hpp \
    text_engine.h \
    point.h \
    rect.h \
    route_renderer.hpp \
    route_shape.hpp \
    window_handle.hpp \
    render_policy.hpp \
    tiling_render_policy_st.hpp \
    tiling_render_policy_mt.hpp \
    simple_render_policy.hpp \
    queued_renderer.hpp \
    basic_tiling_render_policy.hpp \
    tiler.hpp \
    tile.hpp \
    tile_cache.hpp \
    tile_set.hpp \
    tile_renderer.hpp \
    feature_processor.hpp \
    events.hpp \
    coverage_generator.hpp \
    scales_processor.hpp \
    yopme_render_policy.hpp \
