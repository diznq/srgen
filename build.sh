gcc src/SR.c \
    -DTRANSFORM="${TRANSFORM:-transform_rgb3}" \
    -DTRANSFORM_SIZE="${TRANSFORM_SIZE:-3}" \
    -DBLOCK_SIZE_LOG="${BLOCK_SIZE_LOG:-3}" \
    -DTRANSFORMS="${TRANSFORMS:-6}" \
    -DSIMILARITY="${SIMILARITY:-COS}" \
    -funroll-all-loops \
    -Ofast -march=native -mtune=intel -o "${OUTPUT:-bin/SR-gcc.exe}"
