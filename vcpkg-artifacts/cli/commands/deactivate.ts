// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { deactivate } from '../../artifacts/activation';
import { session } from '../../main';
import { Command } from '../command';

export class DeactivateCommand extends Command {
  readonly command = 'deactivate';

  override run() {
    return deactivate(session, true);
  }
}
