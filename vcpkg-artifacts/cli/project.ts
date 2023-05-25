// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../artifacts/activation';
import { Artifact, ResolvedArtifact } from '../artifacts/artifact';
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

function trackActivationPlan(session: Session, resolved: Array<ResolvedArtifact>) {
  for (const resolvedEntry of resolved) {
    const artifact = resolvedEntry.artifact;
    if (artifact instanceof Artifact) {
      session.trackActivate(artifact.registryUri.toString(), artifact.id, artifact.version);
    }
  }
}

export async function activate(session: Session, allowStacking: boolean, stackEntries: Array<string>, artifacts: Array<ResolvedArtifact>, registries: RegistryDisplayContext, options?: ActivationOptions): Promise<boolean> {
  trackActivationPlan(session, artifacts);
  // install the items in the project
  if (!await acquireArtifacts(session, artifacts, registries, options)) {
    return false;
  }

  const activation = await Activation.start(session, allowStacking);
  for (const artifact of artifacts) {
    if (!await artifact.artifact.loadActivationSettings(activation)) {
      return false;
    }
  }

  return await activation.activate(stackEntries, options?.msbuildProps, options?.json);
}
