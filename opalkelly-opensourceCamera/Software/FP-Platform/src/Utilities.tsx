/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

export function sleep(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

/**
 * Repack packed 3-byte-per-pixel GBR wire format into the 4-byte-per-pixel
 * RGBA layout that HTML canvas ImageData uses. Writes numPixels * 4 bytes
 * into dst starting at offset 0. dst must hold at least numPixels * 4 bytes.
 *
 * Source (GBR, packed): src[si+0]=G, src[si+1]=B, src[si+2]=R
 * Target (RGBA, packed): dst[di+0]=R, dst[di+1]=G, dst[di+2]=B, dst[di+3]=A
 */
export function repackGBRtoRGBA(
    src: Uint8Array,
    dst: Uint8ClampedArray,
    numPixels: number
): void {
    for (let i = 0, si = 0, di = 0; i < numPixels; i++, si += 3, di += 4) {
        dst[di + 0] = src[si + 2]; // R
        dst[di + 1] = src[si + 0]; // G
        dst[di + 2] = src[si + 1]; // B
        dst[di + 3] = 0xff;        // A (opaque, wire has no alpha)
    }
}
