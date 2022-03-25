// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary, Strings } from '../collections';

/**
 * types of paths that we can handle when crafting the context
 *
 * Paths has a well-known list of path types that we handle, but we make it a dictionary anyway.
 */

export interface Paths extends Dictionary<Strings> {
  /** entries that should be added to the PATH environment variable */
  bin: Strings;

  /** entries that should be in the INCLUDE environment variable  */
  include: Strings;

  /** entries that should be in the LIB environment variable  */
  lib: Strings;

  /** entries that should be used for GCC's LDSCRIPT */
  ldscript: Strings;

  /** object files that should be linked */
  object: Strings;
}
