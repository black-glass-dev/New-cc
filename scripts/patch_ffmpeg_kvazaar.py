#!/usr/bin/env python3
"""Adds 10-bit (yuv420p10le) support to FFmpeg's libkvazaar encoder wrapper.

FFmpeg n8.1.3 only accepts yuv420p and always opens the 8-bit kvazaar API. The Owncut build links
one kvazaar archive that contains both the 8-bit API (kvz_api_get) and the 10-bit API
(kvz10_api_get, see scripts/build_kvazaar.sh). This patch selects the API from the pixel format
and copies 16-bit samples for yuv420p10le. Idempotent: running it twice changes nothing.

Usage: patch_ffmpeg_kvazaar.py <ffmpeg-source-dir>
"""
import pathlib
import sys


def replace_once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"patch anchor found {count} times, expected 1: {old[:70]!r}")
    return text.replace(old, new)


def main() -> None:
    source = pathlib.Path(sys.argv[1])
    path = source / "libavcodec" / "libkvazaar.c"
    text = path.read_text()
    if "kvz10_api_get" in text:
        print("libkvazaar.c already patched")
        return

    text = replace_once(
        text,
        "static av_cold int libkvazaar_init(AVCodecContext *avctx)\n{\n"
        "    LibkvazaarContext *const ctx = avctx->priv_data;\n"
        "    const kvz_api *const api = ctx->api = kvz_api_get(8);\n",
        "/* 10-bit API from the merged kvazaar archive (see scripts/build_kvazaar.sh). */\n"
        "const kvz_api *kvz10_api_get(int bit_depth);\n\n"
        "static av_cold int libkvazaar_init(AVCodecContext *avctx)\n{\n"
        "    LibkvazaarContext *const ctx = avctx->priv_data;\n"
        "    const int bit_depth = avctx->pix_fmt == AV_PIX_FMT_YUV420P10LE ? 10 : 8;\n"
        "    const kvz_api *const api = ctx->api = bit_depth == 10 ? kvz10_api_get(10) : kvz_api_get(8);\n",
    )
    text = replace_once(
        text,
        "    cfg->height = avctx->height;\n",
        "    cfg->height = avctx->height;\n    cfg->input_bitdepth = bit_depth;\n",
    )
    text = replace_once(
        text,
        "        // Copy pixels from frame to input_pic.\n",
        "        // Copy pixels from frame to input_pic. yuv420p10le stores two bytes per sample.\n"
        "        const int bps = frame->format == AV_PIX_FMT_YUV420P10LE ? 2 : 1;\n",
    )
    text = replace_once(
        text,
        "            int dst_linesizes[4] = {\n"
        "              frame->width,\n"
        "              frame->width / 2,\n"
        "              frame->width / 2,\n",
        "            int dst_linesizes[4] = {\n"
        "              frame->width * bps,\n"
        "              frame->width / 2 * bps,\n"
        "              frame->width / 2 * bps,\n",
    )
    text = replace_once(
        text,
        "static const enum AVPixelFormat pix_fmts[] = {\n    AV_PIX_FMT_YUV420P,\n",
        "static const enum AVPixelFormat pix_fmts[] = {\n    AV_PIX_FMT_YUV420P,\n    AV_PIX_FMT_YUV420P10LE,\n",
    )
    path.write_text(text)
    print("patched libkvazaar.c for yuv420p10le")


if __name__ == "__main__":
    main()
