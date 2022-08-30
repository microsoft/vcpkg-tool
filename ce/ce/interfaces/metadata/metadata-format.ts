// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ProfileBase } from './profile-base';


/**
 * a profile defines the requirements and/or artifact that should be installed
 *
 * Any other keys are considered HostQueries and a matching set of Demands
 * A HostQuery is a query string that can be used to qualify
 * 'requires'/'see-also'/'exports'/'install'/'use' objects
 */
export type Profile = ProfileBase;

/** values that can be either a single string, or an array of strings */
export type StringOrStrings = string | Array<string>;
