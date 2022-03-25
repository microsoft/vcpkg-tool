// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary, Strings } from '../collections';
import { Validation } from '../validation';

/** settings that should be applied to the context */

export interface Settings extends Validation {
  /** a map of path categories to one or more values */
  paths: Dictionary<Strings>;

  /** a map of the known tools to actual tool executable name */
  tools: Dictionary<string>;

  /**
   * a map of (environment) variables that should be set in the context.
   *
   * arrays mean that the values should be joined with spaces
   */
  variables: Dictionary<Strings>;

  /** a map of properties that are activation-type specific (ie, msbuild) */
  properties: Dictionary<Strings>;

  /** a map of locations that are activation-type specific (ie, msbuild) */
  locations: Dictionary<string>;

  // this is where we'd see things like
  // CFLAGS: [...] where you can have a bunch of things that would end up in the CFLAGS variable (or used to set values in a vcxproj/cmake settings file.)
  //
  /**
   * a map of #defines for the artifact.
   *
   * these would likely also be turned into 'variables', but
   * it's significant enough that we need them separately
   */
  defines: Dictionary<string>;
}
