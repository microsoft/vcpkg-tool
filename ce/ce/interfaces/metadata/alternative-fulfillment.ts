// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Strings } from '../collections';
import { Demands } from './demands';
import { Settings } from './Settings';


export interface AlternativeFulfillment extends Demands {
  /** places to look for an executable file  */
  from: Strings;

  /** executable names */
  where: Strings;

  /** command line to run to verify the executable */
  run: string | undefined;

  /** filter to apply to the output */
  select: string | undefined;

  /** the expression to match the selected output with */
  matches: string | undefined;

  /** settings that should be applied to the context when activated if this is a match */
  settings: Settings;
}
