// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { ProjectManifest, ResolvedArtifact, resolveDependencies } from '../artifacts/artifact';
import { i } from '../i18n';
import { trackActivation } from '../insights';
import { RegistryDisplayContext } from '../registries/registries';
import { Session } from '../session';
import { Uri } from '../util/uri';
import { installArtifacts, showArtifacts } from './artifacts';
import { projectFile } from './format';
import { error, log } from './styling';

class ActivationOptions {
  force?: boolean;
  allLanguages?: boolean;
  language?: string;
  msbuildProps?: Uri;
  json?: Uri;
}

export async function openProject(session: Session, location: Uri): Promise<ProjectManifest> {
  // load the project
  return new ProjectManifest(session, await session.openManifest(location));
}

export async function activate(session: Session, artifacts: Array<ResolvedArtifact>, registries: RegistryDisplayContext, createUndoFile: boolean, options?: ActivationOptions) {
  // install the items in the project
  const [success, artifactStatus] = await installArtifacts(artifacts, registries, options);

  if (success) {
    const backupFile = createUndoFile ? session.tmpFolder.join(`previous-environment-${Date.now().toFixed()}.json`) : undefined;
    await session.activation.activate(artifacts, session.environment, session.postscriptFile, backupFile, options?.msbuildProps, options?.json);
  }

  return success;
}

export async function activateProject(session: Session, project: ProjectManifest, options?: ActivationOptions) {
  // track what got installed
  const projectRegistries = await project.buildRegistryResolver();
  const resolved = await resolveDependencies(session, projectRegistries, [project], 3);

  // print the status of what is going to be activated.
  if (!await showArtifacts(resolved, projectRegistries, options)) {
    error(i`Unable to activate project`);
    return false;
  }

  if (await activate(session, resolved, projectRegistries, true, options)) {
    trackActivation();
    log(i`Project ${projectFile(project.metadata.file.parent)} activated`);
    return true;
  }

  log(i`Failed to activate project ${projectFile(project.metadata.file.parent)}`);

  return false;
}
