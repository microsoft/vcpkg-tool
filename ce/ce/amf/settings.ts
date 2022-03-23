// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { Settings as ISettings } from '../interfaces/metadata/Settings';
import { ValidationError } from '../interfaces/validation-error';
import { BaseMap } from '../yaml/BaseMap';
import { ScalarMap } from '../yaml/ScalarMap';
import { StringsMap } from '../yaml/strings';

export class Settings extends BaseMap implements ISettings {
  paths: StringsMap = new StringsMap(undefined, this, 'paths');
  locations: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'locations');
  properties: StringsMap = new StringsMap(undefined, this, 'properties');
  variables: StringsMap = new StringsMap(undefined, this, 'variables');
  tools: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'tools');
  defines: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'defines');

  /** @internal */
  override *validate(): Iterable<ValidationError> {
    // todo: what validations do we need?

  }
}
