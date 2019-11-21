@Library('tf-microsvc-global-shared-library@master')_
node('Dev1') {

    wrap([$class: 'BuildUser']) {
  
    builddesc()

               
    if("${JOB_NAME}" ==~ /(Sandbox)\/(.+)$/) {
        ArtifactoryRepo = "microsvc-mvn-inttest"
        targetArtifactoryRepo = "microsvc-mvn-inttest"
    } else {
        ArtifactoryRepo = "microsvc-mvn-int"
        targetArtifactoryRepo = "microsvc-mvn-int"
    }

    def statusMessage=''
    def ServicePath=''
    def StageName = env.STAGE_NAME
    
    //Echo Job Parameters
    echo "${JOB_NAME}"
    echo 'echoing sandbox detection settings:'
    echo "RepoName: ${repoName} "
    echo "ArtifactoryRepo: ${ArtifactoryRepo}"
    echo "targetArtifactoryRepo: ${targetArtifactoryRepo}" 
    echo 'echoing user selected parameters:'
    echo "ServiceToBuild: ${ServiceToBuild}"
    echo "BranchtoBuild: ${BranchtoBuild}"
    echo "EnvSelect: ${EnvSelect}"
    echo "BuildDesc: ${BuildDesc}"
    echo "Job Name: ${JOB_NAME}"

    try{
        stage('Cleanup'){
            StageName = env.STAGE_NAME
            cleanup()
            sh 'ls -al'
            echo "clean up done"
        
        }

        stage('Checkout'){
            StageName = env.STAGE_NAME
            checkout([
            $class: 'GitSCM',
            branches: [[name: '${BranchtoBuild}']],
            doGenerateSubmoduleConfigurations: false,
            extensions: [],
            submoduleCfg: [],
            userRemoteConfigs: [
                [credentialsId: 'SVC-Jenkins', url: "git@bitbucket.org:tracfonedev/${repoName}.git"]
                ]
            ])
        }

        stage('Sonar Scan'){
            StageName = env.STAGE_NAME
            //sonarScanner()
        }

        stage('Build'){
            StageName = env.STAGE_NAME
            mavenBuild()
        }
        
        stage('Unit Test'){
            StageName = env.STAGE_NAME
            unitTests()
        }

        stage ('Upload Artifact') {
            StageName = env.STAGE_NAME
            uploadArtifact(ArtifactoryRepo,repoName,targetArtifactoryRepo)
        }

        stage ('Generate Config Files') {
            StageName = env.STAGE_NAME
            generateConfigFiles(ArtifactoryRepo,repoName)
        }

        stage ('Deploy') {
            StageName = env.STAGE_NAME
            deployArtifact(ArtifactoryRepo,repoName,targetArtifactoryRepo)
            postDeploy(ArtifactoryRepo,repoName,targetArtifactoryRepo)
            emailNotify(currentBuild.result,StageName)
            echo "RESULT POST: ${currentBuild.result}" 
        }
        
    } catch (e) {
        currentBuild.result = "FAILED"
        emailNotify(currentBuild.result,StageName)
        throw e
        }
    }
}

