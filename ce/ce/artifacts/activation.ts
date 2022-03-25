/* eslint-disable keyword-spacing */
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { delimiter } from 'path';
import { Session } from '../session';
import { linq } from '../util/linq';
import { Uri } from '../util/uri';
import { toXml } from '../util/xml';
import { Artifact } from './artifact';

export class Activation {
  #session: Session;
  constructor(session: Session) {
    this.#session = session;
  }

  /** gets a flattend object representation of the activation */
  get output() {
    return {
      defines: Object.fromEntries(this.defines),
      locations: Object.fromEntries([... this.locations.entries()].map(([k, v]) => [k, v.fsPath])),
      properties: Object.fromEntries([... this.properties.entries()].map(([k, v]) => [k, v.join(',')])),
      environment: { ...process.env, ...Object.fromEntries([... this.environment.entries()].map(([k, v]) => [k, v.join(' ')])) },
      tools: Object.fromEntries(this.tools),
      paths: Object.fromEntries([...this.paths.entries()].map(([k, v]) => [k, v.map(each => each.fsPath).join(delimiter)])),
      aliases: Object.fromEntries(this.aliases)
    };
  }

  generateMSBuild(artifacts: Iterable<Artifact>): string {
    const msbuildFile = {
      Project: {
        $xmlns: 'http://schemas.microsoft.com/developer/msbuild/2003',
        PropertyGroup: <Array<Record<string, any>>>[]
      }
    };

    if (this.locations.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Locations', ...linq.entries(this.locations).toObject(([key, value]) => [key, value.fsPath]) });
    }

    if (this.properties.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Properties', ...linq.entries(this.properties).toObject(([key, value]) => [key, value.join(';')]) });
    }

    if (this.tools.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Tools', ...linq.entries(this.tools).toObject(each => each) });
    }

    if (this.environment.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Environment', ...linq.entries(this.environment).toObject(each => each) });
    }

    if (this.paths.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Paths', ...linq.entries(this.paths).toObject(([key, value]) => [key, value.map(each => each.fsPath).join(';')]) });
    }

    if (this.defines.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Defines', DEFINES: linq.entries(this.defines).select(([key, value]) => `${key}=${value}`).join(';') });
    }

    if (this.aliases.size) {
      msbuildFile.Project.PropertyGroup.push({ $Label: 'Aliases', ...linq.entries(this.environment).toObject(each => each) });
    }

    const propertyGroup = <any>{ $Label: 'Artifacts', Artifacts: { Artifact: [] } };

    for (const artifact of artifacts) {
      propertyGroup.Artifacts.Artifact.push({ $id: artifact.metadata.info.id, '#text': artifact.targetLocation.fsPath });
    }

    if (propertyGroup.Artifacts.Artifact.length > 0) {
      msbuildFile.Project.PropertyGroup.push(propertyGroup);
    }

    return toXml(msbuildFile);
  }

  /** a collection of #define declarations that would assumably be applied to all compiler calls. */
  defines = new Map<string, string>();

  /** a collection of tool definitions from artifacts (think shell 'aliases')  */
  tools = new Map<string, string>();

  /** Aliases are tools that get exposed to the user as shell aliases */
  aliases = new Map<string, string>();

  /** a collection of 'published locations' from artifacts. useful for msbuild */
  locations = new Map<string, Uri>();

  /** a collection of environment variables from artifacts that are intended to be combinined into variables that have PATH delimiters */
  paths = new Map<string, Array<Uri>>();

  /** environment variables from artifacts */
  environment = new Map<string, Array<string>>();

  /** a collection of arbitrary properties from artifacts. useful for msbuild */
  properties = new Map<string, Array<string>>();

  get Paths() {
    // return just paths that have contents.
    return [... this.paths.entries()].filter(([k, v]) => v.length > 0);
  }

  get Variables() {
    // tools + environment
    const result = new Array<[string, string]>();

    // combine variables with spaces
    for (const [key, values] of this.environment) {
      result.push([key, values.join(' ')]);
    }

    // add tools to the list
    for (const [key, value] of this.tools) {
      result.push([key, value]);
    }
    return result;
  }

  get Defines(): Array<[string, string]> {
    return linq.entries(this.defines).toArray();
  }

  get Locations(): Array<[string, string]> {
    return linq.entries(this.locations).select(([k, v]) => <[string, string]>[k, v.fsPath]).where(([k, v]) => v.length > 0).toArray();
  }

  get Properties(): Array<[string, Array<string>]> {
    return linq.entries(this.properties).toArray();
  }

  /** produces an environment block that can be passed to child processes to leverage dependent artifacts during installtion/activation. */
  get environmentBlock(): NodeJS.ProcessEnv {
    const result = this.#session.environment;

    // add environment variables
    for (const [k, v] of this.Variables) {
      result[k] = v;
    }

    // update environment paths
    for (const [variable, values] of this.Paths) {
      if (values.length) {
        const s = new Set(values.map(each => each.fsPath));
        const originalVariable = result[variable] || '';
        if (originalVariable) {
          for (const p of originalVariable.split(delimiter)) {
            if (p) {
              s.add(p);
            }
          }
        }
        result[variable] = originalVariable;
      }
    }

    // define tool environment variables
    for (const [key, value] of this.tools) {
      result[key] = value;
    }

    return result;
  }
}