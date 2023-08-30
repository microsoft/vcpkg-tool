// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { deactivate } from '../../artifacts/activation';
import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';

export class DeactivateCommand extends Command {
  readonly command = 'deactivate';

  get summary() {
    return i`Deactivates the current session`;
  }

  get description() {
    return [
      i`This allows the consumer to remove environment settings for the currently active session.`,
    ];
  }

  override run() {
    return deactivate(session, true);
  }
}
