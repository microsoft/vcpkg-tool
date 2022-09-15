// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Activation } from '../artifacts/activation';
import { buildRegistryResolver, ProjectManifest, ResolvedArtifact, resolveDependencies } from '../artifacts/artifact';
import { i } from '../i18n';
import { trackActivation } from '../insights';
import { RegistryDisplayContext } from '../registries/registries';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { installArtifacts, showArtifacts } from './artifacts';
import { projectFile } from './format';

export interface ActivationOptions {
  force?: boolean;
  allLanguages?: boolean;
  language?: string;
  msbuildProps?: Uri;
  json?: Uri;
}

export async function activate(session: Session, artifacts: Array<ResolvedArtifact>, registries: RegistryDisplayContext, createUndoFile: boolean, options?: ActivationOptions) {
  // install the items in the project
  const [success, artifactStatus] = await installArtifacts(session, artifacts, registries, options);

  if (success) {
    const backupFile = createUndoFile ? session.tmpFolder.join(`previous-environment-${Date.now().toFixed()}.json`) : undefined;
    const activation = new Activation(session);
    for (const artifact of artifacts) {
      if (!await artifact.artifact.loadActivationSettings(activation)) {
        session.channels.error(i`Unable to activate project.`);
        return false;
      }
    }

    await activation.activate(session.environment, session.postscriptFile, backupFile, options?.msbuildProps, options?.json);
  }

  return success;
}

export async function activateProject(session: Session, project: ProjectManifest, options?: ActivationOptions) {
  // track what got installed
  const projectResolver = await buildRegistryResolver(session, project.metadata.registries);
  const resolved = await resolveDependencies(session, projectResolver, [project], 3);

  // print the status of what is going to be activated.
  if (!await showArtifacts(resolved, projectResolver, options)) {
    session.channels.error(i`Unable to activate project`);
    return false;
  }

  if (await activate(session, resolved, projectResolver, true, options)) {
    trackActivation();
    session.channels.message(i`Project ${projectFile(project.metadata.file.parent)} activated`);
    return true;
  }

  session.channels.message(i`Failed to activate project ${projectFile(project.metadata.file.parent)}`);
  return false;
}
