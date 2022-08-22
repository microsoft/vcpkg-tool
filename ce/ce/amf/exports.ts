// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.


import { Exports as IExports } from '../interfaces/metadata/exports';
import { ValidationMessage } from '../interfaces/validation-message';
import { BaseMap } from '../yaml/BaseMap';
import { ScalarMap } from '../yaml/ScalarMap';
import { StringsMap } from '../yaml/strings';

export class Exports extends BaseMap implements IExports {
  aliases: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'aliases');
  defines: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'defines');
  environment: StringsMap = new StringsMap(undefined, this, 'environment');
  locations: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'locations');
  msbuild_properties: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'msbuild-properties');
  paths: StringsMap = new StringsMap(undefined, this, 'paths');
  properties: StringsMap = new StringsMap(undefined, this, 'properties');
  tools: ScalarMap<string> = new ScalarMap<string>(undefined, this, 'tools');

  /** @internal */
  override *validate(): Iterable<ValidationMessage> {
    yield* super.validate();
    yield* this.validateChildKeys([
      'aliases',
      'defines',
      'environment',
      'locations',
      'msbuild-properties',
      'paths',
      'properties',
      'tools'
    ]);
  }
}
