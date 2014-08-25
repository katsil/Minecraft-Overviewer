import math
import os
import os.path
import subprocess
import tempfile
import argparse

from overviewer import blockdefinitions
from overviewer.oil import Matrix
from overviewer import assetpack
from overviewer import chunkrenderer
from overviewer import isometricrenderer
from overviewer import canvas
from overviewer import dispatcher
from overviewer import observer

"""This script takes a block ID and a data value and renders a single image
rendering of that block.

This file serves two purposes: to let users view and test their block models,
and to serve as a test of the block rendering routines.

"""

def main():

    parser = argparse.ArgumentParser(description="Renders an animated GIF of a single block. Used to test the renderer and block models")
    parser.add_argument("blockid", type=int, help="The block ID to render")
    parser.add_argument("data", type=int, help="The data value of the block to render, if applicable", default=0)
    parser.add_argument("--texturepath", type=str, help="specify a resource pack to use", default=None)

    args = parser.parse_args()
    blockid = args.blockid
    data = args.data
    
    ap = assetpack.get_default()
    if args.texturepath:
        other = assetpack.ZipAssetPack(args.texturepath)
        ap = assetpack.CompositeAssetPack([ap, other])

    blockdefs = chunkrenderer.compile_block_definitions(
        ap,
        blockdefinitions.get_default())

    FRAMES = 60

    renderers = []
    for angle in range(0, 360, 360//FRAMES):
        # Standard viewing angle: ~35 degrees, straight down the diagonal of a cube
        matrix = Matrix()
        matrix.scale(500,500,500)
        matrix.rotate(math.atan2(1,math.sqrt(2)),0,0)
        matrix.rotate(0,math.radians(angle),0)
        matrix.rotate(math.radians(45),math.radians(45),0)
        matrix.translate(-0.5,-0.5,-0.5)

        renderers.append(
                isometricrenderer.IsometricSingleBlockRenderer(blockid, data, matrix, blockdefs)
                )

    minx = min(r.get_full_rect()[0] for r in renderers)
    miny = min(r.get_full_rect()[1] for r in renderers)
    maxx = max(r.get_full_rect()[2] for r in renderers)
    maxy = max(r.get_full_rect()[3] for r in renderers)
    
    canvases = []
    filenames = []
    directory = tempfile.mkdtemp()
    for i, r in enumerate(renderers):
        filename = os.path.join(directory, "output_{0:02}.png".format(i))
        canvases.append(
                canvas.SingleImageCanvas((minx, miny), (maxx-minx, maxy-miny), r, filename)
                )
        filenames.append(filename)

    dispatch = dispatcher.Dispatcher()
    dispatch.dispatch_all(canvases, observer=observer.ProgressBarObserver())

    print("Converting to gif...")
    subprocess.call(["convert",
                     "-delay", "5",
                     "-dispose", "Background",
                     ] + filenames + [
                     "output.gif",
                     ])

    print("Cleaning up frame images from {0}".format(directory))
    for filename in filenames:
        os.unlink(filename)

    os.rmdir(directory)

if __name__ == "__main__":
    main()
