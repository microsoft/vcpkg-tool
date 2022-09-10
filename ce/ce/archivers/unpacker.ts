// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { sed } from 'sed-lite';
import { pipeline as origPipeline } from 'stream';
import { promisify } from 'util';
import { InstallEvents } from '../interfaces/events';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { UnpackOptions } from './options';

export const pipeline = promisify(origPipeline);

export interface FileEntry {
  archiveUri: Uri;
  destination: Uri | undefined;
  path: string;
  extractPath: string | undefined;
}

/**
 * Returns a new path string such that the path has prefixCount path elements removed, and directory
 * separators normalized to a single forward slash.
 * If prefixCount is greater than or equal to the number of path elements in the path, undefined is returned.
 */
export function stripPath(path: string, prefixCount: number): string | undefined {
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
export function implementUnpackOptions(path: string, options: UnpackOptions): string | undefined {
  if (options.strip) {
    const maybeStripped = stripPath(path, options.strip);
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

export type Unpacker = (session: Session, archiveUri: Uri, outputUri: Uri, events: Partial<InstallEvents>, options: UnpackOptions) => Promise<void>;
