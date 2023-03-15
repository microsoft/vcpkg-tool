// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../artifacts/activation';
import { ResolvedArtifact } from '../artifacts/artifact';
import { RegistryDisplayContext } from '../registries/registries';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { acquireArtifacts } from './artifacts';

export interface ActivationOptions {
  force?: boolean;
  allLanguages?: boolean;
  language?: string;
  msbuildProps?: Uri;
  json?: Uri;
}

export async function activate(session: Session, allowStacking: boolean, stackEntries: Array<string>, artifacts: Array<ResolvedArtifact>, registries: RegistryDisplayContext, options?: ActivationOptions) : Promise<boolean> {
  // install the items in the project
  const success = await acquireArtifacts(session, artifacts, registries, options);
  if (success) {
    const activation = await Activation.start(session, allowStacking);
    for (const artifact of artifacts) {
      if (!await artifact.artifact.loadActivationSettings(activation)) {
        return false;
      }
    }

    await activation.activate(stackEntries, options?.msbuildProps, options?.json);
  }

  return success;
}
