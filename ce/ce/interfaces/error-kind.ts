// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

export enum ErrorKind {
  SectionNotFound = 'SectionMessing',
  FieldMissing = 'FieldMissing',
  IncorrectType = 'IncorrectType',
  ParseError = 'ParseError',
  DuplicateKey = 'DuplicateKey',
  NoInstallInDemand = 'NoInstallInDemand',
  HostOnly = 'HostOnly',
  MissingHash = 'MissingHashValue',
  InvalidDefinition = 'InvalidDefinition',
}
