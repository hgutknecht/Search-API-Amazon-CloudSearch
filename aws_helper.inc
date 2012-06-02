<?php


/**
 * @file
 * aws_helpder.inc
 * Simple helper include for use with CloudSearch
 *  
*/

require_once 'AWSSDKforPHP/sdk.class.php';
require_once 'AWSSDKforPHP/services/ec2.class.php';
require_once 'AWSSDKforPHP/services/cloudsearch.class.php';

class AWSCloudSearchHelper {
  
  function buildAWSCredentials($key, $secret) {
    $credentials = array();
    $credentials['certificate_authority'] = FALSE;
    $credentials['key'] =  $key;
    $credentials['secret'] = $secret;
    $credentials['default_cache_config'] = 'APC';
    return $credentials;
  }
  
  
  function getAWSCloudSearchInstance_Manual($key, $secret, $region) {
    $search = new AmazonCloudSearch(self::buildAWSCredentials($key, $secret));
    $search->set_region($region);
    return $search;
  }
  
  function getAWSCloudSearchInstance($domain_id) {
     $query = db_query("SELECT domain_name, aws_access_key, aws_secret_key, aws_region from {aws_cloudsearch_domain} WHERE domain_id = :domain_id", array(':domain_id' => $domain_id))->fetch();
     return getAWSCloudSearchInstance_Manual($query->aws_access_key, $query->aws_secret_key, $query->aws_region);
  }
  
  function getAWSRegions($key, $secret) {
    //have to use the EC2 class for this.
    $ec2 = new AmazonEC2(buildAWSCredentials($key, $secret));
    $response = $ec2->describe_regions();
    if (!$response->isOK()) {
      return NULL;
    }
    return $response->body->regionInfo;
  }

}