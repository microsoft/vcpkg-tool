// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Dictionary } from '../collections';
import { Contact } from './contact';
import { Demands } from './demands';
import { Info } from './info';
import { RegistryDeclaration } from './metadata-format';


type Primitive = string | number | boolean;

/**
 * a profile defines the requirements and/or artifact that should be installed
 *
 * Any other keys are considered HostQueries and a matching set of Demands
 * A HostQuery is a query string that can be used to qualify
 * 'requires'/'see-also'/'settings'/'install'/'use' objects
 *
 * @see the section below in this document entitled 'Host/Environment Queries"
 */

export interface ProfileBase extends Demands {
  /** this profile/package information/metadata */
  info: Info;

  /** any contact information related to this profile/package */
  contacts: Dictionary<Contact>; // optional

  /** artifact registries list the references necessary to install artifacts in this file */
  registries?: Dictionary<RegistryDeclaration>;

  /** global settings */
  globalSettings: Dictionary<Primitive | Record<string, unknown>>;

  /** is this document valid */
  readonly isFormatValid: boolean;

  /** parsing errors in this document */
  readonly formatErrors: Array<string>;

  /** does the document pass validation checks? */
  readonly isValid: boolean;

  /** what are the valiation check errors? */
  readonly validationErrors: Array<string>;
}
