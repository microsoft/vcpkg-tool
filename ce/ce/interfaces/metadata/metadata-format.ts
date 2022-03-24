// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ProfileBase } from './profile-base';
import { GitRegistry } from './registries/git-registry';
import { LocalRegistry } from './registries/local-registry';
import { NugetRegistry } from './registries/nuget-registry';


/**
 * a profile defines the requirements and/or artifact that should be installed
 *
 * Any other keys are considered HostQueries and a matching set of Demands
 * A HostQuery is a query string that can be used to qualify
 * 'requires'/'see-also'/'settings'/'install'/'use' objects
 */
export type Profile = ProfileBase;

export type RegistryDeclaration = NugetRegistry | LocalRegistry | GitRegistry;

/** values that can be either a single string, or an array of strings */
export type StringOrStrings = string | Array<string>;
