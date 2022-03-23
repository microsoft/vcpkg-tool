// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { sed } from 'sed-lite';
import { pipeline as origPipeline } from 'stream';
import { promisify } from 'util';
import { InstallEvents, UnpackEvents } from '../interfaces/events';
import { ExtendedEmitter } from '../util/events';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';

export const pipeline = promisify(origPipeline);

export interface FileEntry {
  archiveUri: Uri;
  destination: Uri | undefined;
  path: string;
  extractPath: string | undefined;
}

/** Unpacker base class definition */
export abstract class Unpacker extends ExtendedEmitter<UnpackEvents> {
  /* Event Emitters */

  /** EventEmitter: progress, at least once per file */
  protected progress(archivePercentage: number): void {
    this.emit('progress', archivePercentage);
  }
  protected fileProgress(entry: Readonly<FileEntry>, filePercentage: number): void {
    this.emit('fileProgress', entry, filePercentage);
  }
  /** EventEmitter: unpacked, emitted per file (not per archive)  */
  protected unpacked(entry: Readonly<FileEntry>) {
    this.emit('unpacked', entry);
  }

  abstract unpack(archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions): Promise<void>;

  /**
 * Returns a new path string such that the path has prefixCount path elements removed, and directory
 * separators normalized to a single forward slash.
 * If prefixCount is greater than or equal to the number of path elements in the path, undefined is returned.
 */
  public static stripPath(path: string, prefixCount: number): string | undefined {
    const elements = path.split(/[\\/]+/);
    const hasLeadingSlash = elements.length !== 0 && elements[0].length === 0;
    const hasTrailingSlash = elements.length !== 0 && elements[elements.length - 1].length === 0;
    let countForUndefined = prefixCount;
    if (hasLeadingSlash) {
      ++countForUndefined;
    }

    if (hasTrailingSlash) {
      ++countForUndefined;
    }

    if (elements.length <= countForUndefined) {
      return undefined;
    }

    if (hasLeadingSlash) {
      return '/' + elements.splice(prefixCount + 1).join('/');
    }

    return elements.splice(prefixCount).join('/');
  }

  /**
 * Apply OutputOptions to a path before extraction.
 * @param entry The initial path to a file to unpack.
 * @param options Options to apply to that file name.
 * @returns If the file is to be emitted, the path to use; otherwise, undefined.
 */
  protected static implementOutputOptions(path: string, options: UnpackOptions): string | undefined {
    if (options.strip) {
      const maybeStripped = Unpacker.stripPath(path, options.strip);
      if (maybeStripped) {
        path = maybeStripped;
      } else {
        return undefined;
      }
    }

    if (options.transform) {
      for (const transformExpr of options.transform) {
        if (!path) {
          break;
        }

        const sedTransformExpr = sed(transformExpr);
        path = sedTransformExpr(path);
      }
    }

    return path;
  }
}
