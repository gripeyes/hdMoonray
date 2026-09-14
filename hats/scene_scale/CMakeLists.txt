# Copyright 2026 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

add_hats_test(scene_scale_meter CAMERA camera)
add_hats_test(scene_scale_centimeter CAMERA /camera HUSK)
add_hats_test(scene_scale_override RENDER_SETTINGS /Render/settings)
