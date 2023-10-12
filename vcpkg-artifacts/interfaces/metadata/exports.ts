// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary, Strings } from '../collections';
import { Validation } from '../validation';

/** settings that should be applied to the context */

export interface Exports extends Validation {
  /** shell aliases (aka functions/etc) for exposing specific commands  */
  aliases: Dictionary<string>;
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
  /**
   * a map of (environment) variables that should be set in the context.
   *
   * arrays mean that the values should be joined with spaces
   */
  environment: Dictionary<Strings>;
  /** a map of locations that are activation-type specific */
  locations: Dictionary<string>;
  /** a map of key/values to emit into an MSBuild <PropertyGroup> */
  msbuild_properties: Dictionary<string>;
  /** a map of path categories to one or more values */
  paths: Dictionary<Strings>;
  /** a map of properties that are activation-type specific */
  properties: Dictionary<Strings>;
  /** a map of the known tools to actual tool executable name */
  tools: Dictionary<string>;
}
