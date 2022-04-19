// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Registries } from '../registries/registries';
import { Uri } from '../util/uri';
import { Artifact } from './artifact';

export interface SearchCriteria {
  idOrShortName?: string;
  version?: string
  keyword?: string;
}

export interface Registry {
  readonly count: number;
  readonly location: Uri;
  readonly loaded: boolean;

  search(parent: Registries, criteria?: SearchCriteria): Promise<Array<[Registry, string, Array<Artifact>]>>;

  load(force?: boolean): Promise<void>;
  save(): Promise<void>;
  update(): Promise<void>;
  regenerate(): Promise<void>;
}
