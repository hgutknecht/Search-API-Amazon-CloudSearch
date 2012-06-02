<?php 

/**
 * @file
 * Represents an Amazon CloudSearch Query.  
 * Takes a SeachAPI query and transforms into CloudSearch query.  It then submits this query to the 
 * CloudSearch search endpoint and tranforms the results into the Search API results structure.
 *  
*/

class AWSCloudSearchQuery {
  
  private $_options;
  private $_index_machine_name;
  private $_query;
  
  public function __construct($options) {
    $this->_options = $options;
  }
  
  /**
   * Builds and submits the search query to CloudSearch, sets up Facets and returns the Ids 
   */
  public function execute(SearchApiQueryInterface $query) {
   $query_options = $query->GetOptions();
   $this->_index_machine_name = $query->getIndex()->machine_name;
   $this->_query = $query;
    /* Construct the CloudSearch querystring
    * Params are:
    *   q             - general keyword search, searches all searchable fields
    *   bq            - complex search query, search on multple fields controled by parentheses 
    *   return-fields - what fields to return
    *   facet         - what facets to calculate and return back
    *   start         - control the starting index of the return result
    *   size          - control number of hits to return back
    */
  
    //sort out pager
    $page_size = empty($query_options['limit']) ? 0 : $query_options['limit'];
    $pagenum = pager_find_page()*$page_size;
    $page_size = $page_size > 0 ? "&size={$page_size}" : '';
    $currentpage = "&start={$query_options['offset']}";       
    //Standard keyword search
    $keys = $this->prepareKeys($query->getKeys());
    $q = $keys == '' ? 'q=%': "q={$keys}";
    /* return values, this is pretty simple in SearchAPI, it only requires the item ID, 
     * we add text_relevance to give Search API a score for this result for the default ordering
     */ 
    $rtn = "return-fields=text_relevance";
  
    //add facets (if applicable)  
    $facets = array();
    if ($query->getOption('search_api_facets')) {
      foreach ($query->getOption('search_api_facets') as $key => $facet) {
         $facets[] = AWSCloudSearchDomain::encode_index_name($this->_index_machine_name, $key);
      }
      $f = "facet=";
      foreach ($facets as $facet) {
        $f .= "{$facet},";
      }
      $f = trim($f, ', ');
    }
    
    //Active filters (BQ).  These filters will come from the facets or from the View filters. 
    $bq = "";
    $filters = $query->getFilter();
    if (count($filters->getFilters())>0) {
      $this->createFilterCondition($filters, 0, $bq);
      $bq = 'bq=' . $bq;
    }
  
    //sort.  If no sort specified CloudSearch will default ot text-relevance field
    $sort = $query->getSort();
    $r = '';
    if ($sort) {
      $r = '&rank=';
      foreach ($sort as $field_name => $order) {
          $r .= $order == 'DESC' ? '-' : '' . AWSCloudSearchDomain::encode_index_name($this->_index_machine_name, 'sort_' . $field_name) . ',';
      }
      if ($field_name == 'search_api_relevance') {
          $r .= $order == 'DESC' ? '-' : '' . 'text_relevance';
      }
      if ($field_name == 'search_api_id') {
         $r .= $order == 'DESC' ? '-' : '' . 'id';
      }
      $r = rtrim($r, ',');
    }

    //combine into CloudSearch query string
    $query_params = "{$q}&{$bq}&{$rtn}&{$f}{$r}{$page_size}{$currentpage}";
   
    //send query to CloudSearch endpoint
    $response = json_decode($this->submit_search($query_params));
    
    //check response and report if in error. 
    if ($response == NULL) {
        drupal_set_message(check_plain(t('Critial failure when calling CloudSearch')), 'error');
        watchdog('aws_cloudsearch', 'Critial failure when calling CloudSearch. Request was: %query_string', array('%query_string' => $query_params), WATCHDOG_CRITICAL, $link = NULL);
        return array();
    }
    elseif (isset($response->error)) {
      drupal_set_message(check_plain(t('CloudSearch returned an error:') . $response->messages[0]->message), 'error');
      watchdog('aws_cloudsearch', 'CloudSearch returned an error. Error was %error_message | Request was: %query_string', array('%query_string' => $query_params, '%error_message' => $response->messages[0]->message ), WATCHDOG_CRITICAL, $link = NULL);
      return array();
    }
    
    //CloudSearch request was successfull so build up the results array and hand back to SearchAPI
    $results = array();
    $results['result count'] = $response->hits->found;
    foreach ($response->hits->hit as $hit) {
      $id = str_replace("{$this->_index_machine_name}_", '', $hit->id);
      $results['results'][$id] = array(
        'id' => $id,
        'score' => $hit->data->text_relevance[0],
       );
    }   
    
    //facets
    if (isset($response->facets)) {
      $rtn_facets = array();
      foreach ($response->facets as $key => $facet) {
        if (isset($facet->constraints)) {
          $terms = array();
          foreach ($facet->constraints as $term) {
            $terms[] = array(
              'count' => $term->count,
              'filter' => "\"{$term->value}\"",
            ); 
          }
          $rtn_facets[AWSCloudSearchDomain::decode_index_name($this->_index_machine_name, $key)] = $terms;
        }
      }
      $results['search_api_facets'] = $rtn_facets;
    }
    return $results;
  }

  /**
   *  Helper - creates a CloudSearch filter (bq) string given SearchAPI filter object
   * 
   *  CloudSearch queries use parentheses to group searches and the conjunction is put at the start of each group
   * 
   *  Example:  (AND field_1 field_2 (OR field_3 field 4))
   */
  protected function createFilterCondition(SearchApiQueryFilterInterface $filter, $depth, &$bq='') {
    $bq .= '(' . strtolower($filter->GetConjunction()) . ' ';
    foreach ($filter->getFilters() as $f) {
       if (is_array($f)) {
      $val = $this->getFinalFieldFilterValue($f);
         $fname = AWSCloudSearchDomain::encode_index_name($this->_index_machine_name, $f[0]);
         $bq .= "{$fname}:{$val} "; //trailing space required
       } 
    }
    foreach ($filter->getFilters() as $f) {
      if (is_object($f)) {
        $c = $this->createFilterCondition($f, $depth, $bq);
      }
    }
    $bq .= ') '; //trailing space required
   }

  /**
   * Helper method for getting the filter field ready for AWS.  
   * This is need to ensure uint values are not wrapped in quotes otherwise AWS will fail 
   *  
  */
  private function getFinalFieldFilterValue($field) {
    $type = $this->_query->getIndex()->options['fields'][$field[0]]['type']; 
  if ($type == 'integer') {
      return $field[1];
  }
  else {
      return "'$field[1]'";
  }     
  }
  
  /**
   * Helper method for removing unnecessary nested expressions from keys.
   * Note: Taken from search_api_db 
  */
  protected function prepareKeys($keys) {
    if (is_scalar($keys)) {
      $keys = $this->splitKeys($keys);
      return is_array($keys) ? $this->eliminateDuplicates($keys) : $keys;
    }
    elseif (!$keys) {
      return NULL;
    }
    $keys = $this->eliminateDuplicates($this->splitKeys($keys));
    $conj = $keys['#conjunction'];
    $neg = !empty($keys['#negation']);
    foreach ($keys as $i => &$nested) {
      if (is_array($nested)) {
        $nested = $this->prepareKeys($nested);
        if ($neg == !empty($nested['#negation'])) {
          if ($nested['#conjunction'] == $conj) {
            unset($nested['#conjunction'], $nested['#negation']);
            foreach ($nested as $renested) {
              $keys[] = $renested;
            }
            unset($keys[$i]);
          }
        }
      }
    }
    $keys = array_filter($keys);
    if (($count = count($keys)) <= 2) {
      if ($count < 2 || isset($keys['#negation'])) {
        $keys = NULL;
      }
      else {
        unset($keys['#conjunction']);
        $keys = array_shift($keys);
      }
    }
    return $keys;
  }

  /**
   * Helper method for splitting keys.
   *  Note: Taken from search_api_db
   */
  protected function splitKeys($keys) {
    if (is_scalar($keys)) {
      $proc = drupal_strtolower(trim($keys));
      if (is_numeric($proc)) {
        return ltrim($proc, '-0');
      }
     
      $words = preg_split('/[^\p{L}\p{N}]+/u', $proc, -1, PREG_SPLIT_NO_EMPTY);
      if (count($words) > 1) {
        $proc = $this->splitKeys($words);
        $proc['#conjunction'] = 'AND';
      }
      return $proc;
    }
    foreach ($keys as $i => $key) {
      if (element_child($i)) {
        $keys[$i] = $this->splitKeys($key);
      }
    }
    return array_filter($keys);
  }

  /**
   * Helper method for eliminating duplicates from the search keys.
   *  Note: Taken from search_api_db
   */
  protected function eliminateDuplicates($keys, &$words = array()) {
    foreach ($keys as $i => $word) {
      if (!element_child($i)) {
        continue;
      }
      if (is_scalar($word)) {
        if (isset($words[$word])) {
          unset($keys[$i]);
        }
        else {
          $words[$word] = TRUE;
        }
      }
      else {
        $keys[$i] = $this->eliminateDuplicates($word, $words);
      }
    }
    return $keys;
  }


  /**
   * Performs the actual cURL request to the CloudSearch search endpoint.
   */ 
  private function submit_search($query_params) {
    $url = 'http://' . $this->_options['search_endpoint'] . '/' . $this->_options['cloudsearch_api_version'] . '/search?' . $query_params;
    //only need to encode spaces
    $url = str_replace(' ', '%20', $url);

    if ($this->_options['show_debug']) {
      $msg1 = t('CLOUDSEARCH DEBUG SEARCH URL');
      $msg2 = t('This is the URL that was constructed in order to perform a CloudSearch request:');
      drupal_set_message(filter_xss("<strong>{$msg1}</strong><br/>{$msg2}<br/>") . l($url, $url), 'warning');    
    }
  
  
    $ch = curl_init(); 
    curl_setopt($ch, CURLOPT_URL, $url );
    curl_setopt($ch, CURLOPT_HEADER, 0); 
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    $response = curl_exec($ch); 
    
    if ($this->_options['show_debug']) {
      $msg1 = t('CLOUDSEARCH DEBUG SEARCH RESPONSE');
      $msg2 = t('This is the JSON response from CloudSearch given the URL above:');
      drupal_set_message(filter_xss("<strong>{$msg1}</strong><br/>{$msg2}<br/>") . $response, 'warning');    
    }
    
    curl_close($ch); 
    return $response;
  }
  
  
}