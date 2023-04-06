// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

/** Unpacker output options */

export interface UnpackOptions {
  /**
   * Strip # directories from the path
   *
   * Typically used to remove excessive nested folders off the front of the paths in an archive.
  */
  strip?: number;

  /**
   * A regular expression to transform filenames during unpack. If the resulting file name is empty, it is not emitted.
   */
  transform?: Array<string>;
}
