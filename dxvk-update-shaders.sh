#!/bin/bash
(cd dxvk/src/d3d11/shaders/; for f in *; do glslangValidator --target-env vulkan1.2 --vn ${f%.*} $f -o ../../../include/shaders/${f%.*}.h; done)
(cd dxvk/src/dxvk/shaders/; for f in *; do glslangValidator --target-env vulkan1.2 --vn ${f%.*} $f -o ../../../include/shaders/${f%.*}.h; done)
(cd dxvk/src/dxvk/hud/shaders/; for f in *; do glslangValidator --target-env vulkan1.2 --vn ${f%.*} $f -o ../../../../include/shaders/${f%.*}.h; done)

