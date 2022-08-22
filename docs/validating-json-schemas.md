To validate schemas, you can use the ajv validator.

```
npm install -g ajv ajv-formats ajv-cli
```

For example,

```
ajv validate -c ajv-formats -s .\docs\vcpkg.schema.json -r .\docs\vcpkg-configuration.schema.json -r .\docs\vcpkg-schema-definitions.schema.json -d C:\Dev\vcpkg\ports\**\vcpkg.json --all-errors
```

or


```
ajv validate -c ajv-formats -s .\docs\artifact.schema.json -r .\docs\vcpkg-schema-definitions.schema.json -d C:\Dev\vcpkg-ce-catalog\**\*.json --all-errors
```
