// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../artifacts/activation';
import { ResolvedArtifact } from '../artifacts/artifact';
import { i } from '../i18n';
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

export async function activate(session: Session, artifacts: Array<ResolvedArtifact>, registries: RegistryDisplayContext, options?: ActivationOptions) {
  // install the items in the project
  const success = await acquireArtifacts(session, artifacts, registries, options);
  if (success) {
    const activation = new Activation(session);
    for (const artifact of artifacts) {
      if (!await artifact.artifact.loadActivationSettings(activation)) {
        session.channels.error(i`Unable to activate project`);
        return false;
      }
    }

    await activation.activate(session.nextPreviousEnvironment, options?.msbuildProps, options?.json);
  }

  return success;
}
