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
  
  public static function buildAWSCredentials($key, $secret) {
    $credentials = array();
    $credentials['certificate_authority'] = FALSE;
    $credentials['key'] =  $key;
    $credentials['secret'] = $secret;
    $credentials['default_cache_config'] = 'APC';
    return $credentials;
  }
  
  
  public static function getAWSCloudSearchInstance_Manual($key, $secret, $region) {
    $search = new AmazonCloudSearch(self::buildAWSCredentials($key, $secret));
    $search->set_region($region);
    return $search;
  }
  
  public static function getAWSCloudSearchInstance($domain_id) {
     $query = db_query("SELECT domain_name, aws_access_key, aws_secret_key, aws_region from {aws_cloudsearch_domain} WHERE domain_id = :domain_id", array(':domain_id' => $domain_id))->fetch();
     return getAWSCloudSearchInstance_Manual($query->aws_access_key, $query->aws_secret_key, $query->aws_region);
  }
  
  public static function getAWSRegions($key, $secret) {
    //have to use the EC2 class for this.
    $ec2 = new AmazonEC2(buildAWSCredentials($key, $secret));
    $response = $ec2->describe_regions();
    if (!$response->isOK()) {
      return NULL;
    }
    return $response->body->regionInfo;
  }
  
  public static function getAWSError($response) {
    if (!isset($response->body->Error->Message)) {
      return t('Failed to get AWS error message');
    }
    else {
      return $response->body->Error->Message;      
    }
  }
  
  /**
   * Check the CloudSearch response and log an errors, returning true if OK and false if error
   */
  public static function checkAndReport($context, $response, $throw_exception = FALSE) {
    if (!$response->isOK()) {
      $aws_msg = self::getAWSError($response);
      $msg = t('CloudSearch request failed:') . ' ' . t('Context') . ': ' . $context . ', ' . t('Message') . ': ' . $aws_msg;
      drupal_set_message(check_plain($msg), 'error');
      watchdog('aws_cloudsearch', $msg, NULL, WATCHDOG_CRITICAL, $link = NULL);
      if ($throw_exception) {
        throw new Exception($msg);
      }
      return FALSE;
    }
    else {
      return TRUE;
    }
  }

}