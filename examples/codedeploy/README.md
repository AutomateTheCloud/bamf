# Example: deploying a container with AWS CodeDeploy

A complete set of CodeDeploy hooks, written with BAMF, for an application that runs in Docker on
EC2 instances. Each hook is a short BAMF script; the work is done by the helper files.

| Lifecycle event | Hook | What it does |
|---|---|---|
| ApplicationStop | [`application_stop.sh`](hooks/application_stop.sh) | Stops the running containers (nothing to do on a first deployment) |
| BeforeInstall | [`before_install.sh`](hooks/before_install.sh) | Installs Docker and a pinned, checksum-verified Docker Compose, if missing |
| ApplicationStart | [`application_start.sh`](hooks/application_start.sh) | Writes the settings from SSM Parameter Store to a root-only env file, then starts the containers |
| ValidateService | [`validate_service.sh`](hooks/validate_service.sh) | Waits for the application to answer HTTP 200, so a broken revision fails the deployment |

[`hooks/common.sh`](hooks/common.sh) holds the settings every hook shares. It uses the environment
variables CodeDeploy sets for each hook (`APPLICATION_NAME`, `DEPLOYMENT_GROUP_NAME`), so the same
hooks serve every environment: each deployment group reads its own settings from
`/<application>/<deployment group>/` in Parameter Store.

## Building a revision

The revision bundle holds `appspec.yml`, `app/`, `hooks/`, and a copy of BAMF in `bamf/`:

```shell
mkdir -p bundle/bamf
cp -r examples/codedeploy/appspec.yml examples/codedeploy/app examples/codedeploy/hooks bundle/
cp -r bamf.inc helpers bundle/bamf/
tar -czf app-1.0.0.tar.gz -C bundle .
```

Upload it to S3, then deploy it with
[`aws_codedeploy_create_deployment`](../../helpers/README.md) and
[`aws_codedeploy_wait_for_deployment`](../../helpers/README.md), or from the console.

## What the instances need

- The CodeDeploy agent, and an instance profile allowed to read the parameters
  (`ssm:GetParametersByPath` on the path, and `kms:Decrypt` for SecureString values)
- Outbound access to download Docker and Docker Compose on the first deployment, or an image that
  already has them
