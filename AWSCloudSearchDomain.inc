<?php

/**
 * @file
 * Encapsulates all CloudSearch communication relating to the
 * administration of a Domain.
 *
 * It's key responsiblities are to
 * 		- Manage the creation, modification and removal of CloudSearch indexes
 * 		- Send documents (items) updates to CloudSearch
 * 		- Remove documents (items) from CloudSearch
 * 		- Provide status information about the Domain
 *
 * This class makes use of the AWS SDK for PHP. For more information
 * see @link http://aws.amazon.com/sdkforphp/ documentation @endlink
 */

class AWSCloudSearchDomain {
  protected $domainName;
  protected $cloudSearchInstance;
  protected $domainDocEndpoint;
  protected $documentBatchAction = '/{API_VERSION}/documents/batch';
  protected $indexMachineName;
  protected $sharedFieldMode;
  protected $sharedFieldSiteName;
  protected $serverOptions;

  /**
   * Constructor.

   */
  public function __construct($options, $index_machine_name) {
    $this->serverOptions = $options;
    $this->domainName = $options['domain_name'];
    $this->indexMachineName = $index_machine_name;
    $this->sharedFieldMode = !empty($options['shared_field_mode']) ? $options['shared_field_mode'] : false;
    $this->sharedFieldSiteName = !empty($options['shared_field_site_name']) ? $options['shared_field_site_name'] : '';
    $this->documentBatchAction = str_replace('{API_VERSION}', $options['cloudsearch_api_version'], $this->documentBatchAction);
    $this->cloudSearchInstance = AWSCloudSearchHelper::getAWSCloudSearchInstance($options['aws_access_key'], $options['aws_secret_key'], $options['region']);
    if (!empty($options['document_endpoint'])) {
      $this->domainDocEndpoint = $options['document_endpoint'];
    }

    if ($this->sharedFieldMode) {
      watchdog('search_api_amazon_cloudsearch', 'Shared field mode is enabled with site name: %site.',
        array('%site' => $this->sharedFieldSiteName), WATCHDOG_DEBUG);
    }
  }

  /**
   * Get's the detail of the domain from AWS API.
   */
  public function getDomainDetails() {
    try {
      watchdog('search_api_amazon_cloudsearch', 'Looking up search domain: %domain',
        array('%domain' => $this->domainName), WATCHDOG_DEBUG);

      $details = $this->cloudSearchInstance->describeDomains(array(
          'DomainNames' => array($this->domainName),
      ));
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Unable to find search domain: %domain. Error: .',
        array('%domain' => $this->domainName, '%error' => $e), WATCHDOG_ERROR);

      return;
    }

    /* Check response contains the domain requested, it may have
     * been deleted outside of this module.
     */
    if (count($details['DomainStatusList']) > 0 && $details['DomainStatusList'][0]['DomainName'] == $this->domainName) {
      return $details['DomainStatusList'][0];
    }
    else {
      $msg = t('Could not find domain') . ' ' . $this->domainName . ' ' . t('in the CloudSearch account specified. Please check in the AWS Console and review the health of this domain.');
      drupal_set_message(check_plain($msg), 'error');
      watchdog('search_api_amazon_cloudsearch', $msg, NULL, WATCHDOG_CRITICAL, $link = NULL);

      return false;
    }
  }

  /**
   * Checks to see if the domain is currently active.
   *
   * Looks at all the various status fields for this Domain and returns
   * if it's in an active state. This is an important check before
   * uploading documents to the domain as they won't get uploading
   * if it's not active.
   */
  public function isActive() {
    $details = $this->getDomainDetails();
    if ($details['Processing'] == 'true') {
      return false;
    }
    elseif ($details['RequiresIndexDocuments'] == 'true') {
      return false;
    }
    else {
      return true;
    }
  }

  /**
   * Gets the status of the Domain.
   *
   * Based on the various status fields for that Domain.
   *
   * @return string
   *   A string representation of the Domains status.  Will be
   *    - PROCESSING - Indexes have changed and are being processed
   *    - REQUIRES INDEXING - Index have change but have not yet bene processed
   *    - ACTIVE - Domain is fully available
   */
  public function getStatus() {
    $details = $this->getDomainDetails();

    if ($details['Processing'] == 'true') {
      return 'PROCESSING';
    }
    elseif ($details['RequiresIndexDocuments'] == 'true') {
      return 'REQUIRES INDEXING';
    }
    else {
      return 'ACTIVE';
    }
  }

  /**
   * Adds an Index into CloudSearch.
   *
   * Note, there are terminology differences between SearchAPI and
   * CloudSearch. A SearchAPI index is the entire index, holding all
   * the fields. Whereas CloudSearch index is a field and the domain
   * is the equivalent of the Search API index.
   *
   * @param string $index_field
   *   The name of the index
   *
   * @param string $index_type
   *   The Cloudsearch index type (literal, text, uint)
   *
   * @param string $facet
   *   Should this field be faceted
   *
   * @param string $result
   *   Is this field available to show in the results returned by CloudSearch
   *
   * @param string $searchable
   *   Is this field searchable (text searchable)
   *
   * @param string $source_field
   *   Used if this index should copy it's value from an existing index
   *
   * @return bool
   *   If the add index request worked.
   */
  protected function addIndex($index_field, $index_type, $facet, $result, $searchable, $source_field = NULL, $skip_encode = false) {
    if (!$skip_encode) {
      $index_field = self::encodeIndexName($this->indexMachineName, $index_field);
    }
    if ($source_field != NULL) {
      $source_field = self::encodeIndexName($this->indexMachineName, $source_field);
    }

    $options = array();
    $options['IndexFieldName'] = $index_field;
    $options['IndexFieldType'] = $index_type;

    switch ($index_type) {
      case "text":
        $options['TextOptions'] = array(
          'FacetEnabled' => $facet == true ? true : false,
          'ResultEnabled' => $result == true ? true : false,);
        break;

      case "int":
        // Here for completeness but we leave uint with it's default options.
        break;

      case "literal":
        $options['LiteralOptions'] = array(
          'FacetEnabled' => $facet == true ? true : false,
          'ResultEnabled' => $result == true ? true : false,
          'SearchEnabled' => $searchable == true ? true : false,);
        break;

      default:
        break;

    }

    /* If source field specified then we effectively tell AWS to get's it's
     * value from the field specificed in $source_field.
     */
    if ($source_field != NULL) {
      $options['SourceAttributes'] = array(array(
        'SourceDataFunction' => 'Copy',
        'SourceDataCopy' => array('SourceName' => $source_field),
      )
      );
    }

    watchdog('search_api_amazon_cloudsearch',
      'Defining new CloudSearch index field %fieldname on %domain with options: %options',
      array(
        '%fieldname' => $index_field,
        '%domain' => $this->domainName,
        '%options' => print_r($options, true), WATCHDOG_DEBUG
      )
    );

    try {
      $response = $this->cloudSearchInstance->defineIndexField(array(
        'DomainName' => $this->domainName,
        'IndexField' => $options,
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Failed to add index %index for: %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e, '%index' => $index_field), WATCHDOG_ERROR);

      return false;
    }

    return true;
  }

  /**
   * Removes an Index into CloudSearch.
   *
   * @param string $index_field
   *   The name of the index to remove
   *
   * @return bool
   *   If the remove index request worked.
   */
  protected function removeIndex($index_field) {
    $index_field = self::encodeIndexName($this->indexMachineName, $index_field);

    try {
      $response = $this->cloudSearchInstance->deleteIndexField(array(
        'DomainName' => $this->domainName,
        'IndexFieldName' => $index_field,
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Remove index %index failed for: %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e, '%index' => $index_field), WATCHDOG_ERROR);
      return false;
    }

    return true;
  }

  /**
   * Adds an Index into CloudSearch.
   *
   * @return CFObject
   *   All the index(field) information from the domain(index)
   */
  public function getIndexes() {
    try {
      $response = $this->cloudSearchInstance->describeIndexFields(array(
        'DomainName' => $this->domainName,
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Index document request failed for: %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e), WATCHDOG_ERROR);

      return false;
    }

    // Strip out any fields that are pending deletion otherwise they will be
    // treated as if they exist
    $index_fields = array_filter(
      $response['IndexFields'],
      function($index_field) {
        if (!$index_field['Status']['PendingDeletion']) {
          return $index_field;
        }
      }
    );

    return $index_fields;
  }

  /**
   * Kicks off an Index Documents request within CloudSearch.
   *
   * This will process any modifications to the index.
   *
   * @return bool
   *   Returns true if the request was successful, false if it was not
   */
  protected function indexDocuments() {
    try {
      $response = $this->cloudSearchInstance->indexDocuments(array(
        'DomainName' => $this->domainName,
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Index document request failed for: %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e), WATCHDOG_ERROR);

      return false;
    }

    return true;
  }

  /**
   * Updates any modified CloudSearch indexes given the provided index.
   *
   * @param SearchApiIndex $index
   *   The index to update
   *
   * @return bool
   *   Returns true if the request was successful, false if it was not
   */
  public function updateIndexes(SearchApiIndex $index) {

    // Setup counts in order to report back what was actually changed.
    $deleted_count = 0;
    $new_updated_count = 0;
    $ignored_count = 0;

    // Populate an array of existing fields to remove any indexes.
    $delete_fields = array();

    $found_index_document_field = false;

    /* Get the indexes out of CloudSearch and check them against
     * the modified ones in Drupal.
     */
    $search_api_fields = $index->getFields();
    $current_cs_indexes = $this->getIndexes();

    foreach ($current_cs_indexes as $cs_index) {
      // Unless we are in "shared field mode", only look at fields that are for this index.
      if ($this->startsWith($cs_index['Options']['IndexFieldName'], $this->indexMachineName . '_')) {
        $field_name = self::decodeIndexName($this->indexMachineName, $cs_index['Options']['IndexFieldName']);
        $found = false;
        foreach ($search_api_fields as $field => $info) {
          if ($field == $field_name || 'sort_' . $field == $field_name) {
            $found = true;
          }
        }
        if (!$found) {
          $delete_fields[] = $field_name;
        }
      }

      if ($cs_index['Options']['IndexFieldName'] == 'search_api_document_index_machine_name') {
        $found_index_document_field = true;
      }
    }

    // Now perform the actual delete on the CloudSearch domain.
    foreach ($delete_fields as $field) {
      $response = $this->removeIndex($field);
      $deleted_count++;
    }

    /* Add special index field to store the index machine name
     * against this document
     */
    if (!$found_index_document_field) {
      $this->addIndex('search_api_document_index_machine_name', 'literal', true, false, true, NULL, true);
      $new_updated_count++;
    }

    /* Go through each search api field and add to CloudSearch domain
     * if it has been modified in some way or is totally new.
     */
    foreach ($search_api_fields as $name => $field_info) {
      /* Only do something with this field if it differs from the
       * cloudsearch index in anyway.
       */
      watchdog('search_api_amazon_cloudsearch', 'Comparing %fieldname to CloudSearch index',
        array('%fieldname' => $name), WATCHDOG_DEBUG);

      $is_modified = $this->checkFieldChanged($name, $field_info, $current_cs_indexes);

      if ($is_modified == true) {
        $cs_index_data = $this->searchAPIFieldToCloudSearch($name, $field_info);

        // Localise return fields for brevity.
        $index_type = $cs_index_data['index_type'];
        $facet = $cs_index_data['facet'];
        $searchable = $cs_index_data['searchable'];
        $sortable = $cs_index_data['sortable'];
        $ranges = $cs_index_data['ranges'];
        $result = $cs_index_data['result'];
        $source_fields = $cs_index_data['source_fields'];

        // Add the primary index for this field.
        $this->addIndex($name, $index_type, $facet, $result, $searchable, NULL);

        // Add any additional fields (if it needs to be sortable or ranged).
        foreach ($source_fields as $source_field) {
          $this->addIndex($source_field['name'], $source_field['index_type'], $source_field['faceted'], $source_field['result'], $source_field['searchable'], $name);
        }

        /* Update servers options for this Index.  These will be
         * used by the query process to quickly lookup the CloudSearch types
         */
        $options = $index->server()->options;
        $options['index_options'][$index->machine_name]['fields'][$name] = $cs_index_data;
        $index->server()->update(array('options' => $options));

        $new_updated_count++;
      }
      else {
        $ignored_count++;
      }
    }

    $msg = t('CloudSearch Index update status: Deleted:') . $deleted_count . ' ' . t('Updated/New') . ': ' . $new_updated_count . ' ' . t('Not modified') . ':' . $ignored_count;
    drupal_set_message(check_plain($msg));

    /* Now tell CloudSearch to start indexing.
     * While this process takes place no documents can be uploaded.
     */
    if ($new_updated_count > 0) {
      return $this->indexDocuments();
    }
    else {
      return true;
    }
  }

  /**
   * Helper. Determines if string starts with given string.
   */
  protected function startsWith($haystack, $needle) {
    $length = drupal_strlen($needle);
    return (drupal_substr($haystack, 0, $length) === $needle);
  }

  /**
   * Helper function that determines if the CS index has changed.
   *
   * Compares it to SearchAPI field.
   */
  public function checkFieldChanged($name, $field, $cs_fields) {
    foreach ($cs_fields as $index) {
      $field_name = self::decodeIndexName($this->indexMachineName, $index['Options']['IndexFieldName']);
      if ($name == $field_name) {

        $cs_index_data = $this->searchAPIFieldToCloudSearch($name, $field);
        $index_type = $cs_index_data['index_type'];
        $facet = $cs_index_data['facet'] == true ? true : false;
        $searchable = $cs_index_data['searchable'] == true ? true : false;
        $source_fields = $cs_index_data['source_fields'];

        // If this field should have a sort field then check it has.
        if ($cs_index_data['has_sort_field']) {
          $found = false;
          foreach ($cs_fields as $index_a) {
            $field_name_a = self::decodeIndexName($this->indexMachineName, $index_a['Options']['IndexFieldName']);
            foreach ($source_fields as $sorce_field) {
              if ($field_name_a == $sorce_field['name']) {
                $found = true;
              }
            }
          }
          if (!$found) {
            return true;
          }
        }

        if ((string) $index['Options']['IndexFieldType'] != $index_type) {
          return true;
        }
        if ($index_type == 'literal') {
          if ((string) $index['Options']['LiteralOptions']['FacetEnabled'] != $facet) {
            return true;

          }
          if ((string) $index['Options']['LiteralOptions']['SearchEnabled'] != $searchable) {
            return true;

          }
        }
        return false;
      }
    }
    // If field not found then it's effectively modified.
    return true;

  }

  /**
   * Converts a SearchAPI field into a CloudSearch index.
   *
   * Due to the restrictions of CloudSearch fields additional fields
   * may need to be created to satisfy certain feaures.
   *
   * The primary restrictions are:
   *
   * A CloudSearch Index cannot be sortable and faceted.  Therefore if a
   * faceted field needs to also be sortable a new field needs to be
   * created that holds a copy of the original field and is set to sortable.
   * (Note: by sortable we mean that it's set to appear in the results).
   *
   * If a range search is requried on a number field but it is also faceted
   * then a new field needs to be created.
   *
   * This function is static as it can be used by the real time search process
   * which would not want the added overhead of instantiating
   * the full Domain object.
   */
  public function searchAPIFieldToCloudSearch($name, $field_info) {
    $return_data = array();
    watchdog('search_api_amazon_cloudsearch', 'Translating SearchAPI field type to CloudSearch type for %name with type: %type',
      array('%name' => $name, '%type' => $field_info['type']), WATCHDOG_DEBUG);

    $is_facet = $this->isFieldFacet($name);

    // Setup default return items.
    $return_data['sortable'] = false;
    $return_data['facet'] = $is_facet;
    $return_data['result'] = !$is_facet;
    $return_data['ranges'] = false;
    $return_data['searchable'] = true;
    $return_data['has_sort_field'] = false;

    /* Create an array to hold any additional source fields
     * required because of restrictions in the configuration
     * of CloudSearch index fields.  E.g. a field can't be
     * both sortable and faceted.
     */
    $return_data['source_fields'] = array();

    if (!empty($this->serverOptions['index_options'][$this->indexMachineName]['sorts'][$name])) {
      $return_data['sortable'] = true;
    }
    if (!empty($this->serverOptions['index_options'][$this->indexMachineName]['ranges'][$name])) {
      $return_data['ranges'] = true;
    }

    // Convert Search API type into CloudSearch type (int, literal, text).
    switch ($field_info['type']) {
      case "string":
        $return_data['index_type'] = 'literal';
        break;

      case "date":
        $return_data['index_type'] = $return_data['facet'] == true ? 'literal' : 'int';
        break;

      case "text":
        $return_data['index_type'] = 'text';
        break;

      case "boolean":
        $return_data['index_type'] = $return_data['facet'] == true ? 'literal' : 'int';
        break;

      case "integer":
        $return_data['index_type'] = $return_data['facet'] == true ? 'literal' : 'int';
        break;

      case "decimal":
        $return_data['index_type'] = 'literal';
        break;

      case "list<text>":
        $return_data['index_type'] = 'text-array';
        break;

      case "list<integer>":
        $return_data['index_type'] = 'int-array';
        break;

      default:
        $return_data['index_type'] = 'literal';
        break;
    }

    if (($return_data['sortable'] == true || $return_data['ranges'] == true) && $return_data['facet'] == true) {
      /* CloudSearch text and literal indexes cannot be both
       * sortable and faceted so need to create a new source
       * field.  If the field is a decimal then conver to uint
       * as there is no data type for decimal and we can't
       * sort as a literal datatype.
       *
       * Dev note: For the most accuracy the decimal could be converted
       * to alpha numeric and set as a literal data type.
       */
      $return_data['source_fields'][] = array(
        'name' => 'sort_' . $name,
        'index_type' => self::isNumericFieldType($field_info['type']) ? 'uint' : $return_data['index_type'],
        'result' => true,
        'faceted' => $return_data['ranges'],
        'searchable' => false,
      );

      $return_data['has_sort_field'] = true;
    }

    return $return_data;
  }

  /**
   * Helper - Determines if a Search API field is a numeric.
   */
  public static function isNumericFieldType($field_type) {
    return $field_type == 'decimal' || $field_type == 'integer' || $field_type == 'date';
  }

  /**
   * Helper - Checks the field name against the FacetAPI table.
   */
  protected function isFieldFacet($field_name) {
    $result = db_query('SELECT facet FROM facetapi WHERE searcher = :searcher AND enabled = 1 AND facet = :field_name', array(
      ':searcher' => 'search_api@' . $this->indexMachineName,
      ':field_name' => $field_name)
    );
    return (count($result->fetchAll()) > 0);
  }

  /**
   * Uploads documents into CloudSearch.
   *
   * Converts the items into Json SDF documents and
   * submits to Document endpoint.
   */
  public function updateDocuments($items) {
    $json = array();
    $item_ids = array();
    $field_values = array();
    $excluded_content_types = $this->serverOptions['index_options'][$this->indexMachineName]['excluded_content_types'];
    $skip_counter = 0;

    foreach ($items as $key => $item) {
      if (!empty($excluded_content_types[$item['type']['value']])) {
        $skip_counter++;
        continue;
      }

      // Values array holds all the fields for this item.
      $values = array();


      // Add the standard document index machine name field to document, unless
      // shared_field_mode is enabled, in which case use the defined site name.
      if ($this->sharedFieldMode) {
        $values['search_api_document_index_machine_name'] = $this->sharedFieldSiteName;
      }
      else {
        $values['search_api_document_index_machine_name'] = $this->indexMachineName;
      }

      // Iterate through each field for this item and create JSON SDF doc.
      foreach ($item as $name => $value) {
        /* Converts the Search API field name into a safe CloudSearch field
         * prefixing the index machine name so that this index documents
         * are isollated within the Domain.
         */
        $name = self::encodeIndexName($this->indexMachineName, $name);
        if ($value['value'] != NULL) {

          if ($value['type'] == 'tokens') {

            /* CloudSearch doesn't support tokens like this so
             * just combine it back into a string.
             */
            $combined = '';
            foreach ($value['value'] as $token) {
              $combined .= $token['value'] . ' ';
            }
            $values[$name] = rtrim($combined, ' ');
          }
          elseif (is_array($value['value'])) {
            /* If this is a multi-valued item. Just associate the array
             * with the field values.
             */

            foreach ($value['value'] as $val) {
              $values[$name][] = $val;
            }
          }
          else {
            // Standard single value field.
            $values[$name] = $value['value'];
          }
        }
      }

      $items_ids[] = $item['nid'];

      /* Create the SDF array that will be encoded into Json and sent to AWS.
       *
       * Version: CloudSearch requires a version number.  The version must
       * always be greater than the current document version number
       * else it will not get updated.  We set the version to the current
       * UNIX time as this will guarantee it's the latest version.
       * This is an accepted technique for document versioning.
       *
       * Language: As of 14 June 2012 CloudSearch only supports 'en' language.
       * Given that Search API manages the language using a dedicated
       * index field this isn't strictly required.
       *
       * ID: To ensure this document is unique to this index only the entityid
       * is combined with the index machine name.  This ensures that if
       * more than one index is using the same domain they are kept
       * totally seperate.
       */
      $json[] = array(
        'type' => 'add',
        'id' => self::createId($this->indexMachineName, $key, $this->sharedFieldSiteName),
        'fields' => $values,
      );

    }

    // If no valid matching documents types (e.g. all images were sent) are found, the entire batch will be empty.
    // This is not an error, just skip it and let indexing keep going.
    if (empty($json)) {
      watchdog('search_api_amazon_cloudsearch', 'No valid documents found to update, skipping batch.',
      array(), WATCHDOG_ERROR);
      return true;
    }

    // Encode the SDF array and post to CloudSearch Document endpoint.
    $status = $this->submitJsonRequest(json_encode($json));

    if (!$status) {
      watchdog('search_api_amazon_cloudsearch', 'Failed to update update CloudSearch documents. Attempted batch ids: %batch_ids',
        array('%batch_ids' => print_r($items_ids, true)), WATCHDOG_ERROR);
      return false;
    }

    watchdog('search_api_amazon_cloudsearch', 'Completed updating CloudSearch documents. Skipped %skipped documents.',
      array('%skipped' => $skip_counter), WATCHDOG_INFO);

    return true;
  }

  /**
   * Removes documents from CloudSearch.
   *
   * Converts ids into JSON SDF document and sends to Cloudsearch endpoint.
   * Version is set to current UNIX time to ensure it is the latest
   * version.
   *
   * @param array $ids
   *   An array of ids to remove
   */
  public function removeDocuments($ids) {
    $json = array();
    foreach ($ids as $id) {
      $json[] = array(
        'type' => 'delete',
        'id' => $this->createId($this->indexMachineName, $id, $this->sharedFieldSiteName),
        'version' => time(),
      );
    }

    // Encode the SDF document array and post to Document endpoint.
    $status = $this->submitJsonRequest(json_encode($json));

    if (!$status) {
      watchdog('search_api_amazon_cloudsearch', 'Failed to remove documents from CloudSearch.',
        array(), WATCHDOG_ERROR);
    }

    return true;
  }

  /**
   * Add an ip address to the CloudSearch access policy.
   */
  public function addIPAccessPolicy($ip_address) {
    watchdog('search_api_amazon_cloudsearch', 'Adding IP access policy for %ip on %domain',
      array('%domain' => $this->domainName, '%ip' => $ip_address), WATCHDOG_DEBUG);

    $ip_address = $ip_address . "/32";
    $details = $this->getDomainDetails();
    $domain_arn = (string) $details['ARN'];

    watchdog('search_api_amazon_cloudsearch', 'Describing access policy for %domain.',
      array('%domain' => $this->domainName), WATCHDOG_DEBUG);

    try {
      $response = $this->cloudSearchInstance->describeServiceAccessPolicies(array(
        'DomainName' => $this->domainName,
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Failed to retrieve service access policy for %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e), WATCHDOG_ERROR);

      return false;
    }

    if (gettype($response['AccessPolicies']['Options']) == 'string') {
      $options = json_decode($response['AccessPolicies']['Options']);
    }
    else {
      $options = (object) array();
    }


    // Build up IAM Json document for doc and search endpoint.
    $options->Statement[] = array(
      'Effect' => 'Allow',
      'Action' => '*',
      'Resource' => $domain_arn,
      'Condition' => array('IpAddress' => array('aws:SourceIp' => array($ip_address))));

    try {
      watchdog('search_api_amazon_cloudsearch', 'Updating access policy for %domain.',
        array('%domain' => $this->domainName), WATCHDOG_DEBUG);

      $response = $this->cloudSearchInstance->updateServiceAccessPolicies(array(
        'DomainName' => $this->domainName,
        'AccessPolicies' => json_encode($options),
      )
      );
    }
    catch (Exception $e) {
      watchdog('search_api_amazon_cloudsearch', 'Failed to update service access policy for %domain. Error: %error',
        array('%domain' => $this->domainName, '%error' => $e), WATCHDOG_ERROR);

      return false;
    }

    return true;
  }

  /**
   * Helper - Submits SDF Json document(s) to document endpoint.
   */
  protected function submitJsonRequest($json) {
    watchdog('search_api_amazon_cloudsearch', 'Making json request to %url.',
      array('%url' => 'http://' . $this->domainDocEndpoint . $this->documentBatchAction), WATCHDOG_DEBUG);

    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, 'http://' . $this->domainDocEndpoint . $this->documentBatchAction);
    curl_setopt($ch, CURLOPT_HEADER, 0);
    curl_setopt($ch, CURLOPT_HTTPHEADER, array("Content-Type:application/json"));
    curl_setopt($ch, CURLOPT_NOBODY, 1);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($ch, CURLOPT_POST, 1);
    curl_setopt($ch, CURLOPT_POSTFIELDS, $json);
    $response = curl_exec($ch);
    $status = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    curl_close($ch);

    if ($status != 200) {
      watchdog('search_api_amazon_cloudsearch', 'Received status HTTP %status from CloudWatch API. Response: %response',
        array('%status' => $status, '%response' => $response), WATCHDOG_ERROR);

      return false;
    }

    return true;
  }

  /**
   * Helper - Encodes a search API field name.
   *
   * Makes field clearly identifiable in CloudSearch and seperated
   * from other indexes.
   */
  public static function encodeIndexName($index_name, $field_name) {
    return $index_name . '_' . str_replace(':', '____', $field_name);
  }

  /**
   * Helper - Decodes a CloudSearch index.
   */
  public static function decodeIndexName($index_name, $field_name) {
    return str_replace('____', ':', str_replace($index_name . '_', '', $field_name));
  }

  /**
   * Helper - Create the id for the document/item.
   *
   * Makes it specific to an index by simply
   * combining the Index id and item id.
   */
  protected static function createId($index_id, $item_id, $shared_field_site_name = NULL) {
    if ($shared_field_site_name) {
      return $shared_field_site_name . '_' . $index_id . '_' . $item_id;
    }
    else {
      return $index_id . '_' . $item_id;
    }

  }

}
