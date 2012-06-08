/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHTMLEditRules_h__
#define nsHTMLEditRules_h__

#include "nsTextEditRules.h"
#include "nsIHTMLEditor.h"
#include "nsIEditActionListener.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsEditorUtils.h"
#include "TypeInState.h"
#include "nsReadableUtils.h"
#include "nsTArray.h"
#include "nsRange.h"

class nsIDOMElement;
class nsIEditor;
class nsHTMLEditor;

struct StyleCache : public PropItem
{
  bool mPresent;
  
  StyleCache() : PropItem(), mPresent(false) {
    MOZ_COUNT_CTOR(StyleCache);
  }

  StyleCache(nsIAtom *aTag, const nsAString &aAttr, const nsAString &aValue) : 
             PropItem(aTag, aAttr, aValue), mPresent(false) {
    MOZ_COUNT_CTOR(StyleCache);
  }

  ~StyleCache() {
    MOZ_COUNT_DTOR(StyleCache);
  }
};


#define SIZE_STYLE_TABLE 19

class nsHTMLEditRules : public nsTextEditRules, public nsIEditActionListener
{
public:

  NS_DECL_ISUPPORTS_INHERITED
  
            nsHTMLEditRules();
  virtual   ~nsHTMLEditRules();


  // nsIEditRules methods
  NS_IMETHOD Init(nsPlaintextEditor *aEditor);
  NS_IMETHOD DetachEditor();
  NS_IMETHOD BeforeEdit(nsEditor::OperationID action,
                        nsIEditor::EDirection aDirection);
  NS_IMETHOD AfterEdit(nsEditor::OperationID action,
                       nsIEditor::EDirection aDirection);
  NS_IMETHOD WillDoAction(mozilla::Selection* aSelection, nsRulesInfo* aInfo,
                          bool* aCancel, bool* aHandled);
  NS_IMETHOD DidDoAction(nsISelection *aSelection, nsRulesInfo *aInfo, nsresult aResult);
  NS_IMETHOD DocumentModified();

  nsresult GetListState(bool *aMixed, bool *aOL, bool *aUL, bool *aDL);
  nsresult GetListItemState(bool *aMixed, bool *aLI, bool *aDT, bool *aDD);
  nsresult GetIndentState(bool *aCanIndent, bool *aCanOutdent);
  nsresult GetAlignment(bool *aMixed, nsIHTMLEditor::EAlignment *aAlign);
  nsresult GetParagraphState(bool *aMixed, nsAString &outFormat);
  nsresult MakeSureElemStartsOrEndsOnCR(nsIDOMNode *aNode);

  // nsIEditActionListener methods
  
  NS_IMETHOD WillCreateNode(const nsAString& aTag, nsIDOMNode *aParent, PRInt32 aPosition);
  NS_IMETHOD DidCreateNode(const nsAString& aTag, nsIDOMNode *aNode, nsIDOMNode *aParent, PRInt32 aPosition, nsresult aResult);
  NS_IMETHOD WillInsertNode(nsIDOMNode *aNode, nsIDOMNode *aParent, PRInt32 aPosition);
  NS_IMETHOD DidInsertNode(nsIDOMNode *aNode, nsIDOMNode *aParent, PRInt32 aPosition, nsresult aResult);
  NS_IMETHOD WillDeleteNode(nsIDOMNode *aChild);
  NS_IMETHOD DidDeleteNode(nsIDOMNode *aChild, nsresult aResult);
  NS_IMETHOD WillSplitNode(nsIDOMNode *aExistingRightNode, PRInt32 aOffset);
  NS_IMETHOD DidSplitNode(nsIDOMNode *aExistingRightNode, PRInt32 aOffset, nsIDOMNode *aNewLeftNode, nsresult aResult);
  NS_IMETHOD WillJoinNodes(nsIDOMNode *aLeftNode, nsIDOMNode *aRightNode, nsIDOMNode *aParent);
  NS_IMETHOD DidJoinNodes(nsIDOMNode  *aLeftNode, nsIDOMNode *aRightNode, nsIDOMNode *aParent, nsresult aResult);
  NS_IMETHOD WillInsertText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, const nsAString &aString);
  NS_IMETHOD DidInsertText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, const nsAString &aString, nsresult aResult);
  NS_IMETHOD WillDeleteText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, PRInt32 aLength);
  NS_IMETHOD DidDeleteText(nsIDOMCharacterData *aTextNode, PRInt32 aOffset, PRInt32 aLength, nsresult aResult);
  NS_IMETHOD WillDeleteSelection(nsISelection *aSelection);
  NS_IMETHOD DidDeleteSelection(nsISelection *aSelection);

protected:

  enum RulesEndpoint
  {
    kStart,
    kEnd
  };

  enum BRLocation
  {
    kBeforeBlock,
    kBlockEnd
  };

  // nsHTMLEditRules implementation methods
  nsresult WillInsert(nsISelection *aSelection, bool *aCancel);
  nsresult WillInsertText(  nsEditor::OperationID aAction,
                            mozilla::Selection* aSelection,
                            bool            *aCancel,
                            bool            *aHandled,
                            const nsAString *inString,
                            nsAString       *outString,
                            PRInt32          aMaxLength);
  nsresult WillLoadHTML(nsISelection *aSelection, bool *aCancel);
  nsresult WillInsertBreak(mozilla::Selection* aSelection,
                           bool* aCancel, bool* aHandled);
  nsresult StandardBreakImpl(nsIDOMNode *aNode, PRInt32 aOffset, nsISelection *aSelection);
  nsresult DidInsertBreak(nsISelection *aSelection, nsresult aResult);
  nsresult SplitMailCites(nsISelection *aSelection, bool aPlaintext, bool *aHandled);
  nsresult WillDeleteSelection(mozilla::Selection* aSelection,
                               nsIEditor::EDirection aAction,
                               nsIEditor::EStripWrappers aStripWrappers,
                               bool* aCancel, bool* aHandled);
  nsresult DidDeleteSelection(nsISelection *aSelection, 
                              nsIEditor::EDirection aDir, 
                              nsresult aResult);
  nsresult InsertBRIfNeeded(nsISelection *aSelection);
  nsresult GetGoodSelPointForNode(nsIDOMNode *aNode, nsIEditor::EDirection aAction, 
                                  nsCOMPtr<nsIDOMNode> *outSelNode, PRInt32 *outSelOffset);
  nsresult JoinBlocks(nsCOMPtr<nsIDOMNode> *aLeftBlock, nsCOMPtr<nsIDOMNode> *aRightBlock, bool *aCanceled);
  nsresult MoveBlock(nsIDOMNode *aLeft, nsIDOMNode *aRight, PRInt32 aLeftOffset, PRInt32 aRightOffset);
  nsresult MoveNodeSmart(nsIDOMNode *aSource, nsIDOMNode *aDest, PRInt32 *aOffset);
  nsresult MoveContents(nsIDOMNode *aSource, nsIDOMNode *aDest, PRInt32 *aOffset);
  nsresult DeleteNonTableElements(nsINode* aNode);
  nsresult WillMakeList(nsISelection *aSelection, const nsAString *aListType, bool aEntireList, const nsAString *aBulletType, bool *aCancel, bool *aHandled, const nsAString *aItemType=nsnull);
  nsresult WillRemoveList(nsISelection *aSelection, bool aOrderd, bool *aCancel, bool *aHandled);
  nsresult WillIndent(nsISelection *aSelection, bool *aCancel, bool *aHandled);
  nsresult WillCSSIndent(nsISelection *aSelection, bool *aCancel, bool *aHandled);
  nsresult WillHTMLIndent(nsISelection *aSelection, bool *aCancel, bool *aHandled);
  nsresult WillOutdent(nsISelection *aSelection, bool *aCancel, bool *aHandled);
  nsresult WillAlign(nsISelection *aSelection, const nsAString *alignType, bool *aCancel, bool *aHandled);
  nsresult WillAbsolutePosition(nsISelection *aSelection, bool *aCancel, bool * aHandled);
  nsresult WillRemoveAbsolutePosition(nsISelection *aSelection, bool *aCancel, bool * aHandled);
  nsresult WillRelativeChangeZIndex(nsISelection *aSelection, PRInt32 aChange, bool *aCancel, bool * aHandled);
  nsresult WillMakeDefListItem(nsISelection *aSelection, const nsAString *aBlockType, bool aEntireList, bool *aCancel, bool *aHandled);
  nsresult WillMakeBasicBlock(nsISelection *aSelection, const nsAString *aBlockType, bool *aCancel, bool *aHandled);
  nsresult DidMakeBasicBlock(nsISelection *aSelection, nsRulesInfo *aInfo, nsresult aResult);
  nsresult DidAbsolutePosition();
  nsresult AlignInnerBlocks(nsIDOMNode *aNode, const nsAString *alignType);
  nsresult AlignBlockContents(nsIDOMNode *aNode, const nsAString *alignType);
  nsresult AppendInnerFormatNodes(nsCOMArray<nsIDOMNode>& aArray,
                                  nsINode* aNode);
  nsresult AppendInnerFormatNodes(nsCOMArray<nsIDOMNode>& aArray,
                                  nsIDOMNode *aNode);
  nsresult GetFormatString(nsIDOMNode *aNode, nsAString &outFormat);
  nsresult GetInnerContent(nsIDOMNode *aNode, nsCOMArray<nsIDOMNode>& outArrayOfNodes, PRInt32 *aIndex, bool aList = true, bool aTble = true);
  already_AddRefed<nsIDOMNode> IsInListItem(nsIDOMNode* aNode);
  nsINode* IsInListItem(nsINode* aNode);
  nsresult ReturnInHeader(nsISelection *aSelection, nsIDOMNode *aHeader, nsIDOMNode *aTextNode, PRInt32 aOffset);
  nsresult ReturnInParagraph(nsISelection *aSelection, nsIDOMNode *aHeader, nsIDOMNode *aTextNode, PRInt32 aOffset, bool *aCancel, bool *aHandled);
  nsresult SplitParagraph(nsIDOMNode *aPara,
                          nsIDOMNode *aBRNode, 
                          nsISelection *aSelection,
                          nsCOMPtr<nsIDOMNode> *aSelNode, 
                          PRInt32 *aOffset);
  nsresult ReturnInListItem(nsISelection *aSelection, nsIDOMNode *aHeader, nsIDOMNode *aTextNode, PRInt32 aOffset);
  nsresult AfterEditInner(nsEditor::OperationID action,
                          nsIEditor::EDirection aDirection);
  nsresult RemovePartOfBlock(nsIDOMNode *aBlock, 
                             nsIDOMNode *aStartChild, 
                             nsIDOMNode *aEndChild,
                             nsCOMPtr<nsIDOMNode> *aLeftNode = 0,
                             nsCOMPtr<nsIDOMNode> *aRightNode = 0);
  nsresult SplitBlock(nsIDOMNode *aBlock, 
                      nsIDOMNode *aStartChild, 
                      nsIDOMNode *aEndChild,
                      nsCOMPtr<nsIDOMNode> *aLeftNode = 0,
                      nsCOMPtr<nsIDOMNode> *aRightNode = 0,
                      nsCOMPtr<nsIDOMNode> *aMiddleNode = 0);
  nsresult OutdentPartOfBlock(nsIDOMNode *aBlock, 
                              nsIDOMNode *aStartChild, 
                              nsIDOMNode *aEndChild,
                              bool aIsBlockIndentedWithCSS,
                              nsCOMPtr<nsIDOMNode> *aLeftNode = 0,
                              nsCOMPtr<nsIDOMNode> *aRightNode = 0);

  nsresult ConvertListType(nsIDOMNode* aList,
                           nsCOMPtr<nsIDOMNode>* outList,
                           nsIAtom* aListType,
                           nsIAtom* aItemType);
  nsresult ConvertListType(nsINode* aList,
                           mozilla::dom::Element** aOutList,
                           nsIAtom* aListType,
                           nsIAtom* aItemType);

  nsresult CreateStyleForInsertText(nsISelection *aSelection, nsIDOMDocument *aDoc);
  nsresult IsEmptyBlock(nsIDOMNode *aNode, 
                        bool *outIsEmptyBlock, 
                        bool aMozBRDoesntCount = false,
                        bool aListItemsNotEmpty = false);
  nsresult CheckForEmptyBlock(nsIDOMNode *aStartNode, 
                              nsIDOMNode *aBodyNode,
                              nsISelection *aSelection,
                              bool *aHandled);
  nsresult CheckForInvisibleBR(nsIDOMNode *aBlock, nsHTMLEditRules::BRLocation aWhere, 
                               nsCOMPtr<nsIDOMNode> *outBRNode, PRInt32 aOffset=0);
  nsresult ExpandSelectionForDeletion(nsISelection *aSelection);
  bool IsFirstNode(nsIDOMNode *aNode);
  bool IsLastNode(nsIDOMNode *aNode);
  nsresult NormalizeSelection(nsISelection *inSelection);
  nsresult GetPromotedPoint(RulesEndpoint aWhere, nsIDOMNode *aNode,
                            PRInt32 aOffset, nsEditor::OperationID actionID,
                            nsCOMPtr<nsIDOMNode> *outNode, PRInt32 *outOffset);
  nsresult GetPromotedRanges(nsISelection *inSelection, 
                             nsCOMArray<nsIDOMRange> &outArrayOfRanges, 
                             nsEditor::OperationID inOperationType);
  nsresult PromoteRange(nsIDOMRange *inRange,
                        nsEditor::OperationID inOperationType);
  nsresult GetNodesForOperation(nsCOMArray<nsIDOMRange>& inArrayOfRanges, 
                                nsCOMArray<nsIDOMNode>& outArrayOfNodes, 
                                nsEditor::OperationID inOperationType,
                                bool aDontTouchContent=false);
  nsresult GetChildNodesForOperation(nsIDOMNode *inNode, 
                                     nsCOMArray<nsIDOMNode>& outArrayOfNodes);
  nsresult GetNodesFromPoint(DOMPoint point,
                             nsEditor::OperationID operation,
                             nsCOMArray<nsIDOMNode>& arrayOfNodes,
                             bool dontTouchContent);
  nsresult GetNodesFromSelection(nsISelection *selection,
                                 nsEditor::OperationID operation,
                                 nsCOMArray<nsIDOMNode>& arrayOfNodes,
                                 bool aDontTouchContent=false);
  nsresult GetListActionNodes(nsCOMArray<nsIDOMNode> &outArrayOfNodes, bool aEntireList, bool aDontTouchContent=false);
  nsresult GetDefinitionListItemTypes(nsIDOMNode *aNode, bool &aDT, bool &aDD);
  nsresult GetParagraphFormatNodes(nsCOMArray<nsIDOMNode>& outArrayOfNodes, bool aDontTouchContent=false);
  nsresult LookInsideDivBQandList(nsCOMArray<nsIDOMNode>& aNodeArray);
  nsresult BustUpInlinesAtRangeEndpoints(nsRangeStore &inRange);
  nsresult BustUpInlinesAtBRs(nsIDOMNode *inNode, 
                              nsCOMArray<nsIDOMNode>& outArrayOfNodes);
  nsCOMPtr<nsIDOMNode> GetHighestInlineParent(nsIDOMNode* aNode);
  nsresult MakeTransitionList(nsCOMArray<nsIDOMNode>& inArrayOfNodes, 
                              nsTArray<bool> &inTransitionArray);
  nsresult RemoveBlockStyle(nsCOMArray<nsIDOMNode>& arrayOfNodes);
  nsresult ApplyBlockStyle(nsCOMArray<nsIDOMNode>& arrayOfNodes, const nsAString *aBlockTag);
  nsresult MakeBlockquote(nsCOMArray<nsIDOMNode>& arrayOfNodes);
  nsresult SplitAsNeeded(const nsAString *aTag, nsCOMPtr<nsIDOMNode> *inOutParent, PRInt32 *inOutOffset);
  nsresult AddTerminatingBR(nsIDOMNode *aBlock);
  nsresult JoinNodesSmart( nsIDOMNode *aNodeLeft, 
                           nsIDOMNode *aNodeRight, 
                           nsCOMPtr<nsIDOMNode> *aOutMergeParent, 
                           PRInt32 *aOutMergeOffset);
  nsresult GetTopEnclosingMailCite(nsIDOMNode *aNode, nsCOMPtr<nsIDOMNode> *aOutCiteNode, bool aPlaintext);
  nsresult PopListItem(nsIDOMNode *aListItem, bool *aOutOfList);
  nsresult RemoveListStructure(nsIDOMNode *aList);
  nsresult CacheInlineStyles(nsIDOMNode *aNode);
  nsresult ReapplyCachedStyles();
  nsresult ClearCachedStyles();
  nsresult AdjustSpecialBreaks(bool aSafeToAskFrames = false);
  nsresult AdjustWhitespace(nsISelection *aSelection);
  nsresult PinSelectionToNewBlock(nsISelection *aSelection);
  nsresult CheckInterlinePosition(nsISelection *aSelection);
  nsresult AdjustSelection(nsISelection *aSelection, nsIEditor::EDirection aAction);
  nsresult FindNearSelectableNode(nsIDOMNode *aSelNode, 
                                  PRInt32 aSelOffset, 
                                  nsIEditor::EDirection &aDirection,
                                  nsCOMPtr<nsIDOMNode> *outSelectableNode);
  nsresult InDifferentTableElements(nsIDOMNode *aNode1, nsIDOMNode *aNode2, bool *aResult);
  nsresult RemoveEmptyNodes();
  nsresult SelectionEndpointInNode(nsINode *aNode, bool *aResult);
  nsresult UpdateDocChangeRange(nsIDOMRange *aRange);
  nsresult ConfirmSelectionInBody();
  nsresult InsertMozBRIfNeeded(nsIDOMNode *aNode);
  bool     IsEmptyInline(nsIDOMNode *aNode);
  bool     ListIsEmptyLine(nsCOMArray<nsIDOMNode> &arrayOfNodes);
  nsresult RemoveAlignment(nsIDOMNode * aNode, const nsAString & aAlignType, bool aChildrenOnly);
  nsresult MakeSureElemStartsOrEndsOnCR(nsIDOMNode *aNode, bool aStarts);
  nsresult AlignBlock(nsIDOMElement * aElement, const nsAString * aAlignType, bool aContentsOnly);
  nsresult RelativeChangeIndentationOfElementNode(nsIDOMNode *aNode, PRInt8 aRelativeChange);
  void DocumentModifiedWorker();

// data members
protected:
  nsHTMLEditor           *mHTMLEditor;
  nsRefPtr<nsRange>       mDocChangeRange;
  bool                    mListenerEnabled;
  bool                    mReturnInEmptyLIKillsList;
  bool                    mDidDeleteSelection;
  bool                    mDidRangedDelete;
  bool                    mRestoreContentEditableCount;
  nsRefPtr<nsRange>       mUtilRange;
  PRUint32                mJoinOffset;  // need to remember an int across willJoin/didJoin...
  nsCOMPtr<nsIDOMNode>    mNewBlock;
  nsRangeStore            mRangeItem;
  StyleCache              mCachedStyles[SIZE_STYLE_TABLE];
};

#endif //nsHTMLEditRules_h__

