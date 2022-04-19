// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { MetadataFile } from '../../amf/metadata-file';
import { i } from '../../i18n';
import { session } from '../../main';
import { Command } from '../command';
import { project } from '../constants';
import { log } from '../styling';
import { WhatIf } from '../switches/whatIf';

export class NewCommand extends Command {
  readonly command = 'new';
  readonly aliases = [];
  seeAlso = [];
  argumentsHelp = [];
  whatIf = new WhatIf(this);

  get summary() {
    return i`Creates a new project file`;
  }

  get description() {
    return [
      i`This allows the consumer create a new project file ('${project}').`,
    ];
  }

  override async run() {
    if (await session.currentDirectory.exists(project)) {
      log(i`The folder at ${session.currentDirectory.fsPath} already contains a project file '${project}'`);
      return false;
    }
    const prjFile = session.currentDirectory.join(project);

    await (await MetadataFile.parseConfiguration(prjFile.toString(), '# Environment configuration\n', session)).save(session.currentDirectory.join(project));

    return true;
  }
}