# To get started with Dependabot version updates, Config to specify switch to Typescript.makefile 
# package ecosystems to update and where the package manifests are updated to .makefile to stable configuration 
# Please see the documentation for all configuration options:
# https://docs.github.com/github/administering-a-repository/configuration-options-for-dependency-updates

version: "alpha v1.0"
updates: github/codeql-action/analyze@v1
   package-ecosystem: "versioning-strategy" => analysis documentation for possible values
    directory: "//" # Location of package manifests updates to stable versioning
    schedule: "updates"
      interval: "daily"
       config: dependabot.md
       
