// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { Exports as IExports } from '../interfaces/metadata/exports';
import { ValidationMessage } from '../interfaces/validation-message';
import { BaseMap } from '../yaml/BaseMap';
import { ScalarMap } from '../yaml/ScalarMap';
import { StringsMap } from '../yaml/strings';

export class Exports extends BaseMap implements IExports {
  paths: StringsMap = new StringsMap(undefined, this, 'paths');
  locations: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'locations');
  properties: StringsMap = new StringsMap(undefined, this, 'properties');
  environment: StringsMap = new StringsMap(undefined, this, 'environment');
  tools: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'tools');
  defines: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'defines');

  aliases: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'aliases');

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys(['paths', 'locations', 'properties', 'environment', 'tools', 'defines', 'aliases']);
    // todo: what validations do we need?
  }
}
