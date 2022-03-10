// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Strings } from '../../collections';


export interface UnpackSettings {
  /** a number of levels of directories to strip off the front of the file names in the archive when restoring (think tar --strip 1) */
  strip?: number;

  /** one or more transform strings to apply to the filenames as they are restored (think tar --xform ... ) */
  transform: Strings;
}
